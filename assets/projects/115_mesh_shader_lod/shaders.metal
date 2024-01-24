#include <metal_stdlib>
using namespace metal;

// Object function group size
#define AS_GROUP_SIZE 32

struct SceneProperties {
    float4x4    CameraVP;
    uint        InstanceCount;
    uint        MeshletCount;
    uint        __pad0[2];
    uint        Meshlet_LOD_Offsets[5];
    uint        Meshlet_LOD_Counts[5];
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

struct Instance
{
    float4x4 M;
};

struct MeshVertex {
	float4 PositionCS [[position]];
	float3 Color;
};

struct Payload {
    uint InstanceIndices[AS_GROUP_SIZE];
    uint MeshletIndices[AS_GROUP_SIZE];
};

// -------------------------------------------------------------------------------------------------
// Object Function
// -------------------------------------------------------------------------------------------------
[[object]]
void objectMain(
    constant SceneProperties&  Scene         [[buffer(0)]],
    device const float4*       MeshletBounds [[buffer(1)]],    
    device const Instance*     Instances     [[buffer(2)]],
    uint                       gtid          [[thread_position_in_threadgroup]],
    uint                       dtid          [[thread_position_in_grid]],
    object_data Payload&       outPayload    [[payload]],
    mesh_grid_properties       outGrid)
{
    uint visible = 0;

    uint instanceIndex = dtid / Scene.MeshletCount;
    uint meshletIndex  = dtid % Scene.MeshletCount;
   
    if (instanceIndex < Scene.InstanceCount) {
        uint lod             = instanceIndex;
        uint lodMeshletCount = Scene.Meshlet_LOD_Counts[lod];

        if (meshletIndex < lodMeshletCount) {
            meshletIndex += Scene.Meshlet_LOD_Offsets[lod];

            // Transform meshlet's bounding sphere into world space
            float4x4 M = Instances[instanceIndex].M;
            float4 meshletBoundingSphere = M * float4(MeshletBounds[meshletIndex].xyz, 1.0);
            meshletBoundingSphere.w = MeshletBounds[meshletIndex].w;

            // Assuming visibile, no culling here
            visible = 1;
        }
    }

    if (visible) {
        uint index = simd_prefix_exclusive_sum(visible);
        outPayload.InstanceIndices[index] = instanceIndex;
        outPayload.MeshletIndices[index]  = meshletIndex;
    }

    // Assumes all meshlets are visible
    uint visibleCount = simd_sum(visible);
    if (gtid == 0) {
        outGrid.set_threadgroups_per_grid(uint3(visibleCount, 1, 1));
    }
}

// -------------------------------------------------------------------------------------------------
// Mesh Function
// -------------------------------------------------------------------------------------------------
using MeshOutput = metal::mesh<MeshVertex, void, 128, 256, topology::triangle>;

[[mesh]]
void meshMain(
    constant SceneProperties&  Scene                 [[buffer(0)]],
    device const Vertex*       Vertices              [[buffer(1)]],
    device const Meshlet*      Meshlets              [[buffer(2)]],
    device const float4*       MeshletBounds         [[buffer(3)]],
    device const uint*         MeshletVertexIndices  [[buffer(4)]],
    device const uint*         MeshletTriangeIndices [[buffer(5)]],
    device const Instance*     Instances             [[buffer(6)]],
    object_data const Payload& payload               [[payload]],
    uint                       gtid                  [[thread_position_in_threadgroup]],
    uint                       gid                   [[threadgroup_position_in_grid]],
    MeshOutput                 outMesh)
{
    uint instanceIndex = payload.InstanceIndices[gid];
    uint meshletIndex = payload.MeshletIndices[gid];

    device const Meshlet& m = Meshlets[meshletIndex];
    outMesh.set_primitive_count(m.TriangleCount);

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

        float4x4 MVP = Scene.CameraVP * Instances[instanceIndex].M;

        MeshVertex vtx;
        vtx.PositionCS = MVP * float4(Vertices[vertexIndex].Position, 1.0);
        vtx.Color = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8);

        outMesh.set_vertex(gtid, vtx);   
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
