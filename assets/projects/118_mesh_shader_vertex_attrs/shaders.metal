#include <metal_stdlib>
using namespace metal;

// Object function group size
#define AS_GROUP_SIZE 32

enum DrawFunc {
    DRAW_FUNC_POSITION  = 0,
    DRAW_FUNC_TEX_COORD = 1,
    DRAW_FUNC_NORMAL    = 2,
    DRAW_FUNC_PHONG     = 3,
};

struct SceneProperties {
    float4x4      InstanceM;
    float4x4      CameraVP;
    packed_float3 EyePosition;
    uint          DrawFunc;
    packed_float3 LightPosition;
};

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

struct MeshVertex {
	float4 PositionCS [[position]];
    float3 PositionWS;
    float3 Normal;
    float2 TexCoord;
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
    outGrid.set_threadgroups_per_grid(uint3(AS_GROUP_SIZE, 1, 1));
}

// -------------------------------------------------------------------------------------------------
// Mesh Function
// -------------------------------------------------------------------------------------------------
using MeshOutput = metal::mesh<MeshVertex, void, 128, 256, topology::triangle>;

[[mesh]]
void meshMain(
    constant SceneProperties&   Scene                 [[buffer(0)]],
    device const packed_float3* Positions             [[buffer(1)]],
    device const packed_float2* TexCoords             [[buffer(2)]],
    device const packed_float3* Normals               [[buffer(3)]],
    device const Meshlet*       Meshlets              [[buffer(4)]],
    device const uint*          MeshletVertexIndices  [[buffer(5)]],
    device const uint*          MeshletTriangeIndices [[buffer(6)]],
    object_data const Payload&  payload               [[payload]],
    uint                        gtid                  [[thread_position_in_threadgroup]],
    uint                        gid                   [[threadgroup_position_in_grid]],
    MeshOutput                  outMesh)
{
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

        // Fetch object space position
        float4 PositionOS = float4(Positions[vertexIndex], 1.0);
        // Calculate world space position
        float4 PositionWS = Scene.InstanceM * PositionOS;

        // Set vertex data
        MeshVertex vtx;
        vtx.PositionCS = Scene.CameraVP * PositionWS;
        vtx.PositionWS = PositionWS.xyz;
        vtx.Normal     = (Scene.InstanceM * float4(Normals[vertexIndex], 0.0)).xyz;
        vtx.TexCoord   = TexCoords[vertexIndex];

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
float4 fragmentMain(
    FSInput                   input [[stage_in]],
    constant SceneProperties& Scene [[buffer(0)]])
{
    float3 color = input.vtx.PositionWS;
    if (Scene.DrawFunc == DRAW_FUNC_TEX_COORD) {
        color = float3(input.vtx.TexCoord, 0.0);
    }
    else if (Scene.DrawFunc == DRAW_FUNC_NORMAL) {
        color = input.vtx.Normal;
    }
    else if (Scene.DrawFunc == DRAW_FUNC_PHONG) {
        float3 V = normalize(Scene.EyePosition - input.vtx.PositionWS);
        float3 L = normalize(Scene.LightPosition - input.vtx.PositionWS);
        float3 H = normalize(V + L);
        float3 N = normalize(input.vtx.Normal);
        float  NoL = saturate(dot(N, L));
        float  NoH = saturate(dot(N, H));

        float d = NoL;
        float s = pow(NoH, 50.0);
        float a = 0.65;
        
        color = float3(0.549, 0.556, 0.554) * (s + d + a);
    }

	return float4(color, 1.0);
}
