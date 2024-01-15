
#ifdef __spirv__
#define DEFINE_AS_PUSH_CONSTANT [[vk::push_constant]]
#else
#define DEFINE_AS_PUSH_CONSTANT
#endif

#define AS_GROUP_SIZE 32

struct CameraProperties {
    float4x4 MVP;
};

DEFINE_AS_PUSH_CONSTANT
ConstantBuffer<CameraProperties> Cam : register(b0);

struct Vertex {
    float3 Position;
};

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

StructuredBuffer<Vertex>  Vertices        : register(t1);
StructuredBuffer<Meshlet> Meshlets        : register(t2);
StructuredBuffer<uint>    VertexIndices   : register(t3);
StructuredBuffer<uint>    TriangleIndices : register(t4);

struct MeshOutput {
    float4 Position : SV_POSITION;
    float3 Color    : COLOR;
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
        // aligned to 4 and we can easily grab it to unpack.
        //
        uint packed = TriangleIndices[m.TriangleOffset/4 + gtid];
        uint vIdx0  = (packed >>  0) & 0xFF;
        uint vIdx1  = (packed >>  8) & 0xFF;
        uint vIdx2  = (packed >> 16) & 0xFF;
        triangles[gtid] = uint3(vIdx0, vIdx1, vIdx2);
    }

    if (gtid < m.VertexCount) {
        uint vertexIndex = m.VertexOffset + gtid;        
        vertexIndex = VertexIndices[vertexIndex];

        vertices[gtid].Position = mul(Cam.MVP, float4(Vertices[vertexIndex].Position, 1.0));
        
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
