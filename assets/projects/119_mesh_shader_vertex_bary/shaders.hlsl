
#define AS_GROUP_SIZE 32

enum DrawFunc {
    DRAW_FUNC_POSITION  = 0,
    DRAW_FUNC_TEX_COORD = 1,
    DRAW_FUNC_NORMAL    = 2,
    DRAW_FUNC_PHONG     = 3,
};

struct SceneProperties {
    float4x4 InstanceM;
    float4x4 CameraVP;
    float3   EyePosition;
    uint     DrawFunc;
    float3   LightPosition;
};

ConstantBuffer<SceneProperties> Scene : register(b0);

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

StructuredBuffer<float3>  Positions            : register(t1);
StructuredBuffer<float2>  TexCoords            : register(t2);
StructuredBuffer<float3>  Normals              : register(t3);
StructuredBuffer<Meshlet> Meshlets             : register(t4);
StructuredBuffer<uint>    MeshletVertexIndices : register(t5);
StructuredBuffer<uint>    MeshletTriangles     : register(t6);

struct MeshOutput {
    float4               PositionCS  : SV_POSITION;
    nointerpolation uint VertexIndex : VERTEX_INDEX;
};

struct Payload {
    uint MeshletIndices[AS_GROUP_SIZE];
};

groupshared Payload sPayload;

// -------------------------------------------------------------------------------------------------
// Amplification Shader
// -------------------------------------------------------------------------------------------------
[shader("amplification")]
[numthreads(AS_GROUP_SIZE, 1, 1)]
void asmain(
    uint gtid : SV_GroupThreadID,
    uint dtid : SV_DispatchThreadID,
    uint gid  : SV_GroupID
)
{
    sPayload.MeshletIndices[gtid] = dtid;
    // Assumes all meshlets are visible
    DispatchMesh(AS_GROUP_SIZE, 1, 1, sPayload);
}

// -------------------------------------------------------------------------------------------------
// Mesh Shader
// -------------------------------------------------------------------------------------------------
[shader("mesh")]
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void msmain(
                 uint       gtid : SV_GroupThreadID, 
                 uint       gid  : SV_GroupID, 
     in payload  Payload    payload, 
    out indices  uint3      triangles[128], 
    out vertices MeshOutput vertices[64]) 
{
    uint meshletIndex = payload.MeshletIndices[gid];

    Meshlet m = Meshlets[meshletIndex];
    SetMeshOutputCounts(m.VertexCount, m.TriangleCount);
       
    if (gtid < m.TriangleCount) {
        //
        // meshopt stores the triangle offset in bytes since it stores the
        // triangle indices as 3 consecutive bytes. 
        //
        // Since we repacked those 3 bytes to a 32-bit uint, our offset is now
        // aligned to 4 and we can easily grab it as a uint without any 
        // additional offset math.
        //
        uint packed = MeshletTriangles[m.TriangleOffset + gtid];
        uint vIdx0  = (packed >>  0) & 0xFF;
        uint vIdx1  = (packed >>  8) & 0xFF;
        uint vIdx2  = (packed >> 16) & 0xFF;
        triangles[gtid] = uint3(vIdx0, vIdx1, vIdx2);
    }

    if (gtid < m.VertexCount) {
        uint vertexIndex = m.VertexOffset + gtid;        
        vertexIndex = MeshletVertexIndices[vertexIndex];

        float4 PositionOS = float4(Positions[vertexIndex], 1.0);
        float4 PositionWS = mul(Scene.InstanceM, PositionOS);

        vertices[gtid].PositionCS  = mul(Scene.CameraVP, PositionWS);
        vertices[gtid].VertexIndex = vertexIndex;
    }
}

// -------------------------------------------------------------------------------------------------
// Pixel Shader
// -------------------------------------------------------------------------------------------------
#define USE_QUAD_SHUFFLE 1

struct PSInput {
    float4               PositionCS  : SV_POSITION;
    nointerpolation uint VertexIndex : VERTEX_INDEX;
    float3               Bary        : SV_BARYCENTRICS;
};

[shader("pixel")]
float4 psmain(PSInput input) : SV_TARGET
{
    // Get triangle's vertex indices
    uint vIdx0 = GetAttributeAtVertex(input.VertexIndex, 0);
    uint vIdx1 = GetAttributeAtVertex(input.VertexIndex, 1);
    uint vIdx2 = GetAttributeAtVertex(input.VertexIndex, 2);

#if defined(USE_QUAD_SHUFFLE)
    //
    // Use quad shuffle as for barycentric interpolation similar to:
    //   https://github.com/nvpro-samples/gl_vk_meshlet_cadscene/commit/77f82988d39752e061ff00e7e8de9347afe70cc3
    //
    // NOTE: GetAttributeAtVertex() currently can't take a variable for the 
    //       second param (VertexID) so we have to use a if block.
    //
    int quadId = (int)(WaveGetLaneIndex() % 4);

    uint vIdx = 0;
    if (quadId == 0) vIdx = GetAttributeAtVertex(input.VertexIndex, 0);
    else if (quadId == 1) vIdx = GetAttributeAtVertex(input.VertexIndex, 1);
    else if (quadId == 2) vIdx = GetAttributeAtVertex(input.VertexIndex, 2);    

    float3 position = Positions[vIdx];
    float3 position0 = QuadReadLaneAt(position, 0);
    float3 position1 = QuadReadLaneAt(position, 1);
    float3 position2 = QuadReadLaneAt(position, 2);
    position = (position0 * input.Bary.x) + (position1 * input.Bary.y) + (position2 * input.Bary.z);
    position = mul(Scene.InstanceM, float4(position, 1.0)).xyz;    

    float2 texCoord = TexCoords[vIdx];
    float2 texCoord0 = QuadReadLaneAt(texCoord, 0);
    float2 texCoord1 = QuadReadLaneAt(texCoord, 1);
    float2 texCoord2 = QuadReadLaneAt(texCoord, 2);
    texCoord = (texCoord0 * input.Bary.x) + (texCoord1 * input.Bary.y) + (texCoord2 * input.Bary.z);

    float3 normal = Normals[vIdx];
    float3 normal0 = QuadReadLaneAt(normal, 0);
    float3 normal1 = QuadReadLaneAt(normal, 1);
    float3 normal2 = QuadReadLaneAt(normal, 2);
    normal = (normal0 * input.Bary.x) + (normal1 * input.Bary.y) + (normal2 * input.Bary.z);
    normal = mul(Scene.InstanceM, float4(normal, 0.0)).xyz;    
#else
    // Interpolate position using barycentrics
    float3 position0 = Positions[vIdx0];
    float3 position1 = Positions[vIdx1];
    float3 position2 = Positions[vIdx2];
    float3 position   = (position0 * input.Bary.x) + (position1 * input.Bary.y) + (position2 * input.Bary.z);
    position = mul(Scene.InstanceM, float4(position, 1.0)).xyz;

    // Interpolate tex coord using barycentrics
    float2 texCoord0 = TexCoords[vIdx0];
    float2 texCoord1 = TexCoords[vIdx1];
    float2 texCoord2 = TexCoords[vIdx2];
    float2 texCoord  = (texCoord0 * input.Bary.x) + (texCoord1 * input.Bary.y) + (texCoord2 * input.Bary.z);

    // Interpolate normal using barycentrics
    float3 normal0 = Normals[vIdx0];
    float3 normal1 = Normals[vIdx1];
    float3 normal2 = Normals[vIdx2];
    float3 normal   = (normal0 * input.Bary.x) + (normal1 * input.Bary.y) + (normal2 * input.Bary.z);
    normal = mul(Scene.InstanceM, float4(normal, 0.0)).xyz;
#endif

    float3 color = position;
    if (Scene.DrawFunc == DRAW_FUNC_TEX_COORD) {
        color = float3(texCoord, 0.0);
    }
    else if (Scene.DrawFunc == DRAW_FUNC_NORMAL) {
        color = normal;
    }
    else if (Scene.DrawFunc == DRAW_FUNC_PHONG) {
        float3 V = normalize(Scene.EyePosition - position);
        float3 L = normalize(Scene.LightPosition - position);
        float3 H = normalize(V + L);
        float3 N = normalize(normal);
        float  NoL = saturate(dot(N, L));
        float  NoH = saturate(dot(N, H));

        float d = NoL;
        float s = pow(NoH, 50.0);
        float a = 0.335;
        
        color = float3(1.000, 0.766, 0.326) * (s + d + a);
    }

    return float4(color, 1);
}
