
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
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : POSITIONWS;
    float3 Normal     : NORMAL;
    float2 TexCoord   : TEXCOORD;
};

struct Payload {
    uint MeshletIndices[AS_GROUP_SIZE];
};

groupshared Payload sPayload;

// -------------------------------------------------------------------------------------------------
// Amplification Shader
// -------------------------------------------------------------------------------------------------
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

        vertices[gtid].PositionCS = mul(Scene.CameraVP, PositionWS);
        vertices[gtid].PositionWS = PositionWS.xyz;
        vertices[gtid].Normal     = mul(Scene.InstanceM, float4(Normals[vertexIndex], 0.0)).xyz;
        vertices[gtid].TexCoord   = TexCoords[vertexIndex];
    }
}

// -------------------------------------------------------------------------------------------------
// Pixel Shader
// -------------------------------------------------------------------------------------------------
float4 psmain(MeshOutput input) : SV_TARGET
{
    float3 color = input.PositionWS;
    if (Scene.DrawFunc == DRAW_FUNC_TEX_COORD) {
        color = float3(input.TexCoord, 0.0);
    }
    else if (Scene.DrawFunc == DRAW_FUNC_NORMAL) {
        color = input.Normal;
    }
    else if (Scene.DrawFunc == DRAW_FUNC_PHONG) {
        float3 V = normalize(Scene.EyePosition - input.PositionWS);
        float3 L = normalize(Scene.LightPosition - input.PositionWS);
        float3 H = normalize(V + L);
        float3 N = normalize(input.Normal);
        float  NoL = saturate(dot(N, L));
        float  NoH = saturate(dot(N, H));

        float d = NoL;
        float s = pow(NoH, 50.0);
        float a = 0.65;
        
        color = float3(0.549, 0.556, 0.554) * (s + d + a);
    }

    return float4(color, 1);
}
