#include <metal_stdlib>
using namespace metal;

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

using MeshOutput = metal::mesh<MeshVertex, void, 128, 256, topology::triangle>;

[[mesh]]
void meshMain(
    constant CameraProperties& Cam                   [[buffer(0)]],
    device const Vertex*       Vertices              [[buffer(1)]],
    device const Meshlet*      Meshlets              [[buffer(2)]],
    device const uint*         MeshletVertexIndices  [[buffer(3)]],
    device const uint*         MeshletTriangeIndices [[buffer(4)]],
    uint                       gtid                  [[thread_position_in_threadgroup]],
    uint                       gid                   [[threadgroup_position_in_grid]],
    MeshOutput                 outMesh)
{
    device const Meshlet& m = Meshlets[gid];

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

    /*
    Vertex vertices[3];
    
    vertices[0].PositionCS = float4(-0.5, 0.5, 0.0, 1.0);
    vertices[0].Color = float3(1.0, 0.0, 0.0);

    vertices[1].PositionCS = float4(0.5, 0.5, 0.0, 1.0);
    vertices[1].Color = float3(0.0, 1.0, 0.0);

    vertices[2].PositionCS = float4(0.0, -0.5, 0.0, 1.0);
    vertices[2].Color = float3(0.0, 0.0, 1.0);
    
    outMesh.set_vertex(0, vertices[0]);
    outMesh.set_vertex(1, vertices[1]);
    outMesh.set_vertex(2, vertices[2]);
    
    outMesh.set_index(0, 0);
    outMesh.set_index(1, 1);
    outMesh.set_index(2, 2);
    
    outMesh.set_primitive_count(3);
    */
}

struct FSInput
{
    MeshVertex vtx;
};

[[fragment]]
float4 fragmentMain(FSInput input [[stage_in]])
{
	return float4(input.vtx.Color, 1.0);
}
