#include <metal_stdlib>
using namespace metal;

struct SceneProperties {
    float4x4      M;
    float4x4      VP;
    float3        eyePosition;
    float3        lightPosition;
};

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

struct MeshVertex {
	float4 PositionCS [[position]];
    //float3 PositionWS;
    //float2 TexCoord;
    //float3 Normal;
    //float3 Color;
    uint VertexIndex [[flat]];
};

using MeshOutput = metal::mesh<MeshVertex, void, 128, 256, topology::triangle>;

[[mesh]]
void meshMain(
    constant SceneProperties&   Scene                 [[buffer(0)]],
    device const packed_float3* VertexPositions       [[buffer(1)]],
    device const packed_float2* VertexTexCoords       [[buffer(2)]],
    device const packed_float3* VertexNormals         [[buffer(3)]],
    device const Meshlet*       Meshlets              [[buffer(4)]],
    device const uint*          MeshletVertexIndices  [[buffer(5)]],
    device const uint*          MeshletTriangeIndices [[buffer(6)]],
    uint                        gtid                  [[thread_position_in_threadgroup]],
    uint                        gid                   [[threadgroup_position_in_grid]],
    MeshOutput                  outMesh)
{
    device const Meshlet& m = Meshlets[gid];
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

        float4 PositionWS4 = Scene.M * float4(VertexPositions[vertexIndex], 1.0);

        MeshVertex vtx;
        vtx.PositionCS = Scene.VP * PositionWS4;
        //vtx.PositionWS = PositionWS4.xyz;
        //vtx.TexCoord = VertexTexCoords[vertexIndex];
        //vtx.Normal = (Scene.M * float4(VertexNormals[vertexIndex], 0.0)).xyz;
        //vtx.Color = float3(
        //    float(gid & 1),
        //    float(gid & 3) / 4,
        //    float(gid & 7) / 8);
        vtx.VertexIndex = vertexIndex;

        outMesh.set_vertex(gtid, vtx);   
    }
}

struct FSInput
{
    uint VertexIndex [[flat]];
};

[[fragment]]
float4 fragmentMain(
    FSInput                     input           [[stage_in]],
    float3                      bary            [[barycentric_coord]],
    constant SceneProperties&   Scene           [[buffer(0)]],
    device const packed_float3* VertexPositions [[buffer(1)]],
    device const packed_float2* VertexTexCoords [[buffer(2)]],
    device const packed_float3* VertexNormals   [[buffer(3)]],
    uint                        qid             [[thread_index_in_quadgroup]],
    uint                        pid             [[primitive_id]]
)
{
    //uint vIdx = input.vtx.VertexIndex;
    /*
    uint vIdx;
    if (qid == 0) {
        vIdx = quad_broadcast(input.vtx.VertexIndex, 0);
    }
    if (qid == 1) {
        vIdx = quad_broadcast(input.vtx.VertexIndex, 1);
    }
    if (qid == 2) {
        vIdx = quad_broadcast(input.vtx.VertexIndex, 2);
    }
    if (qid == 3) {
        vIdx = quad_broadcast(input.vtx.VertexIndex, 3);
    }
    */

    /*
    uint vIdx = input.VertexIndex;

    float3 P = VertexPositions[vIdx];
    float2 uv = VertexTexCoords[vIdx];

    float2 color = quad_broadcast(uv, 0) * bary.x +
                   quad_broadcast(uv, 1) * bary.y +
                   quad_broadcast(uv, 2) * bary.z);
    */

    //float3 color = quad_shuffle(input.vtx.VertexIndex / 65536.0, 0);

    
    uint vIdx = input.VertexIndex;

    float3 color = float3(0, 0, 0);
    if (vIdx == quad_broadcast(vIdx, 1)) {
        color = float3(1, 0, 0);
    }
    else if (vIdx == quad_broadcast(vIdx, 0)) {
        color = float3(0, 1, 0);
    }
    else if (vIdx == quad_broadcast(vIdx, 2)) {
        color = float3(0, 0, 1);
    }
    

    //float3 color = float3(input.VertexIndex / 1024.0, 0, 0);

    return float4(color, 1);

    /*
    float3 V = normalize(Scene.eyePosition - input.vtx.PositionWS);
    float3 L = normalize(Scene.lightPosition - input.vtx.PositionWS);
    float3 H = normalize(V + L);
    float3 N = normalize(input.vtx.Normal);
    float  NoL = saturate(dot(N, L));
    float  NoH = saturate(dot(N, H));

    float d = NoL;
    float s = pow(NoH, 50.0);
    float a = 0.65;
    
    float3 color = float3(0.549, 0.556, 0.554) * (s + d + a);

    return float4(color, 1.0);
    */
}