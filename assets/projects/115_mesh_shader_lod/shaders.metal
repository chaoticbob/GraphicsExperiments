#include <metal_stdlib>
using namespace metal;

// Object function group size
#define AS_GROUP_SIZE 32

enum {
    FRUSTUM_PLANE_LEFT   = 0,
    FRUSTUM_PLANE_RIGHT  = 1,
    FRUSTUM_PLANE_TOP    = 2,
    FRUSTUM_PLANE_BOTTOM = 3,
    FRUSTUM_PLANE_NEAR   = 4,
    FRUSTUM_PLANE_FAR    = 5,
};

enum VisibilityFunc
{
    VISIBILITY_FUNC_NONE                = 0,
    VISIBILITY_FUNC_PLANES              = 1,
    VISIBILITY_FUNC_SPHERE              = 2,
    VISIBILITY_FUNC_CONE                = 3,
    VISIBILITY_FUNC_CONE_AND_NEAR_PLANE = 4,
};

struct FrustumPlane {
    packed_float3 Normal;
    float         __pad0;
    packed_float3 Position;
    float        __pad1;
};

struct FrustumCone {
    packed_float3 Tip;
    float         Height;
    packed_float3 Direction;
    float         Angle;
};

struct FrustumData {
    FrustumPlane  Planes[6];
    float4        Sphere;
    FrustumCone   Cone;
};

struct SceneProperties {
    float4x4    CameraVP;
    FrustumData Frustum;
    uint        InstanceCount;
    uint        MeshletCount;
    uint        VisibilityFunc;
    uint        __pad0;
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
float SignedPointPlaneDistance(float3 P, float3 planeN, float3 planeP)
{
    float d = dot(normalize(planeN), P - planeP);
    return d;
};

bool VisibleFrustumPlanes(constant const SceneProperties& Scene, float4 sphere)
{
    float d0 = SignedPointPlaneDistance(sphere.xyz, Scene.Frustum.Planes[0].Normal, Scene.Frustum.Planes[0].Position);
    float d1 = SignedPointPlaneDistance(sphere.xyz, Scene.Frustum.Planes[1].Normal, Scene.Frustum.Planes[1].Position);
    float d2 = SignedPointPlaneDistance(sphere.xyz, Scene.Frustum.Planes[2].Normal, Scene.Frustum.Planes[2].Position);
    float d3 = SignedPointPlaneDistance(sphere.xyz, Scene.Frustum.Planes[3].Normal, Scene.Frustum.Planes[3].Position);
    float d4 = SignedPointPlaneDistance(sphere.xyz, Scene.Frustum.Planes[4].Normal, Scene.Frustum.Planes[4].Position);
    float d5 = SignedPointPlaneDistance(sphere.xyz, Scene.Frustum.Planes[5].Normal, Scene.Frustum.Planes[5].Position);

    // Determine if we're on the positive half space of frustum planes
    bool pos0 = (d0 >= 0);
    bool pos1 = (d1 >= 0);
    bool pos2 = (d2 >= 0);
    bool pos3 = (d3 >= 0);
    bool pos4 = (d4 >= 0);
    bool pos5 = (d5 >= 0);

    bool inside = pos0 && pos1 && pos2 && pos3 && pos4 && pos5;
    return inside;
};

bool VisibleFrustumSphere(constant const SceneProperties& Scene, float4 sphere)
{
    // Intersection or inside with frustum sphere
    bool inside = (distance(sphere.xyz, Scene.Frustum.Sphere.xyz) < (sphere.w + Scene.Frustum.Sphere.w));
    return inside;
};

bool VisibleFrustumCone(constant const SceneProperties& Scene, float4 sphere)
{
    // Cone and sphere are within intersectable range
    float3 v0 = sphere.xyz - Scene.Frustum.Cone.Tip;
    float  d0 = dot(v0, Scene.Frustum.Cone.Direction);
    bool   i0 = (d0 <= (Scene.Frustum.Cone.Height + sphere.w));

    float cs = cos(Scene.Frustum.Cone.Angle * 0.5);
    float sn = sin(Scene.Frustum.Cone.Angle * 0.5);
    float a  = dot(v0, Scene.Frustum.Cone.Direction);
    float b  = a * sn / cs;
    float c  = sqrt(dot(v0, v0) - (a * a));
    float d  = c - b;
    float e  = d * cs;
    bool  i1 = (e < sphere.w);

    return i0 && i1;
};

bool VisibleFrustumConeAndNearPlane(constant const SceneProperties& Scene, float4 sphere) 
{
    bool i0 = VisibleFrustumCone(Scene, sphere);

    FrustumPlane frNear = Scene.Frustum.Planes[FRUSTUM_PLANE_NEAR];
    float d0 = SignedPointPlaneDistance(sphere.xyz, frNear.Normal, frNear.Position);
    bool  i1 = (abs(d0) < sphere.w); // Intersects with near plane
    bool  i2 = (d0 > 0);             // On positive half space of near plane

    return i0 && (i1 || i2);
};

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
            
            if (Scene.VisibilityFunc == VISIBILITY_FUNC_NONE) {
                visible = 1;
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_PLANES) {
                visible = VisibleFrustumPlanes(Scene, meshletBoundingSphere) ? 1 : 0;
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_SPHERE) {
                visible = VisibleFrustumSphere(Scene, meshletBoundingSphere) ? 1 : 0;
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_CONE) {
                visible = VisibleFrustumCone(Scene, meshletBoundingSphere) ? 1 : 0;
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_CONE_AND_NEAR_PLANE) {
                visible = VisibleFrustumConeAndNearPlane(Scene, meshletBoundingSphere) ? 1 : 0;
            }
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
