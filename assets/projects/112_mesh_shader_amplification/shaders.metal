#include <metal_stdlib>
using namespace metal;

// Object function group size
#define AS_GROUP_SIZE 32

struct CameraProperties {
    float4x4 MVP;
};

struct Vertex {
    packed_float3 Position;
};

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

struct MeshVertex {
	float4 PositionCS [[position]];
	float3 Color;
};

struct Payload {
    uint MeshletIndices[AS_GROUP_SIZE];
};

// -------------------------------------------------------------------------------------------------
// Object Function
// -------------------------------------------------------------------------------------------------
[[object]]
void objectMain(
    uint                 gtid       [[thread_position_in_threadgroup]],
    uint                 dtid       [[thread_position_in_grid]],
    object_data Payload& outPayload [[payload]],
    mesh_grid_properties outGrid)
{
    outPayload.MeshletIndices[gtid] = dtid;
    // Assumes all meshlets are visible
    if (gtid == 0) {
        outGrid.set_threadgroups_per_grid(uint3(AS_GROUP_SIZE, 1, 1));
    }
}

// -------------------------------------------------------------------------------------------------
// Mesh Function
// -------------------------------------------------------------------------------------------------
using MeshOutput = metal::mesh<MeshVertex, void, 128, 256, topology::triangle>;

[[mesh]]
void meshMain(
    constant CameraProperties& Cam                   [[buffer(0)]],
    device const Vertex*       Vertices              [[buffer(1)]],
    device const Meshlet*      Meshlets              [[buffer(2)]],
    device const uint*         MeshletVertexIndices  [[buffer(3)]],
    device const uint*         MeshletTriangeIndices [[buffer(4)]],
    object_data const Payload& payload               [[payload]],
    uint                       gtid                  [[thread_position_in_threadgroup]],
    uint                       gid                   [[threadgroup_position_in_grid]],
    MeshOutput                 outMesh)
{
    uint meshletIndex = payload.MeshletIndices[gid];
    device const Meshlet& m = Meshlets[meshletIndex];

    if (gtid < m.TriangleCount) {
        //
        // meshopt stores the triangle offset in bytes since it stores the
        // triangle indices as 3 consecutive bytes. 
        //
        // Since we repacked those 3 bytes to a 32-bit uint, our offset is now
        // aligned to 4 and we can easily grab it as a uint without any 
        // additional offset math.
        //
        uint packed = MeshletTriangeIndices[m.TriangleOffset + gtid];
        uint vIdx0  = (packed >>  0) & 0xFF;
        uint vIdx1  = (packed >>  8) & 0xFF;
        uint vIdx2  = (packed >> 16) & 0xFF;
        
        uint triIdx = 3 * gtid;
        outMesh.set_index(triIdx + 0, vIdx0);
        outMesh.set_index(triIdx + 1, vIdx1);
        outMesh.set_index(triIdx + 2, vIdx2);
    }

    if (gtid < m.VertexCount) {
        uint vertexIndex = m.VertexOffset + gtid;
        vertexIndex = MeshletVertexIndices[vertexIndex];

        MeshVertex vtx;
        vtx.PositionCS = Cam.MVP * float4(Vertices[vertexIndex].Position, 1.0);
        vtx.Color = float3(
            float(gid & 1),
            float(gid & 3) / 4,
            float(gid & 7) / 8);

        outMesh.set_vertex(gtid, vtx);   
    }

    // Should be called at end
    if (gtid == 0) {
        outMesh.set_primitive_count(m.TriangleCount);
    }
}

// -------------------------------------------------------------------------------------------------
// Fragment Shader
// -------------------------------------------------------------------------------------------------
struct FSInput
{
    MeshVertex vtx;
};

[[fragment]]
float4 fragmentMain(FSInput input [[stage_in]])
{
	return float4(input.vtx.Color, 1.0);
}
