
#ifdef __spirv__
#define DEFINE_AS_PUSH_CONSTANT [[vk::push_constant]]
#else
#define DEFINE_AS_PUSH_CONSTANT
#endif

#define AS_GROUP_SIZE 32

struct SceneProperties {
    float4x4 CameraVP;
    uint     MeshletCount;
};

DEFINE_AS_PUSH_CONSTANT
ConstantBuffer<SceneProperties> Scene : register(b0);

struct Vertex {
    float3 Position;
};

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

struct Instance
{
    float4x4 M;
};

StructuredBuffer<Vertex>   Vertices        : register(t1);
StructuredBuffer<Meshlet>  Meshlets        : register(t2);
StructuredBuffer<uint>     VertexIndices   : register(t3);
StructuredBuffer<uint>     TriangleIndices : register(t4);
StructuredBuffer<Instance> Instances       : register(t5);

struct MeshOutput {
    float4 Position : SV_POSITION;
    float3 Color    : COLOR;
};

struct Payload {
    uint InstanceIndices[AS_GROUP_SIZE];
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
    sPayload.InstanceIndices[gtid] = dtid / Scene.MeshletCount;    
    sPayload.MeshletIndices[gtid]  = dtid % Scene.MeshletCount;
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
    out indices  uint3      triangles[126], 
    out vertices MeshOutput vertices[64]) 
{
    uint instanceIndex = payload.InstanceIndices[gid];
    uint meshletIndex = payload.MeshletIndices[gid];

    if (meshletIndex >= Scene.MeshletCount) {
        return;
    }

    Meshlet m = Meshlets[meshletIndex];
    SetMeshOutputCounts(m.VertexCount, m.TriangleCount);
       
    if (gtid < m.TriangleCount) {
        uint packed = TriangleIndices[m.TriangleOffset/4 + gtid];
        uint vIdx0  = (packed >>  0) & 0xFF;
        uint vIdx1  = (packed >>  8) & 0xFF;
        uint vIdx2  = (packed >> 16) & 0xFF;
        triangles[gtid] = uint3(vIdx0, vIdx1, vIdx2);
    }

    if (gtid < m.VertexCount) {
        uint vertexIndex = m.VertexOffset + gtid;        
        vertexIndex = VertexIndices[vertexIndex];

        float4x4 MVP = mul(Scene.CameraVP, Instances[instanceIndex].M);

        vertices[gtid].Position = mul(MVP, float4(Vertices[vertexIndex].Position, 1.0));
        
        float3 color = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8);
        vertices[gtid].Color = color;
    }
}

// -------------------------------------------------------------------------------------------------
// Pixel Shader
// -------------------------------------------------------------------------------------------------
float4 psmain(MeshOutput input) : SV_TARGET
{
    return float4(input.Color, 1);
}