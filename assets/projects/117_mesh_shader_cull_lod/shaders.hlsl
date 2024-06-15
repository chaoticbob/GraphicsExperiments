
#define AS_GROUP_SIZE 32
#define MAX_LOD_COUNT 5

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
    float3 Normal;
    float  __pad0;
    float3 Position;
    float  __pad1;
};

struct FrustumCone {
    float3 Tip;
    float  Height;
    float3 Direction;
    float  Angle;
};

struct FrustumData {
    FrustumPlane  Planes[6];
    float4        Sphere;
    FrustumCone   Cone;
};

struct SceneProperties {
    float3      EyePosition;
    float4x4    CameraVP;
    FrustumData Frustum;
    uint        InstanceCount;
    uint        MeshletCount;
    uint        VisibilityFunc;
    float       MaxLODDistance;
    uint        Meshlet_LOD_Offsets[5];
    uint        Meshlet_LOD_Counts[5];
    uint3       __pad1;
    float3      MeshBoundsMin;
    float3      MeshBoundsMax;
    uint        EnableLOD;
};

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
StructuredBuffer<float4>   MeshletBounds   : register(t3);
StructuredBuffer<uint>     VertexIndices   : register(t4);
StructuredBuffer<uint>     TriangleIndices : register(t5);
StructuredBuffer<Instance> Instances       : register(t6);

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
float SignedPointPlaneDistance(float3 P, float3 planeN, float3 planeP)
{
    float d = dot(normalize(planeN), P - planeP);
    return d;
};

bool VisibleFrustumPlanes(float4 sphere)
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

bool VisibleFrustumSphere(float4 sphere)
{
    // Intersection or inside with frustum sphere
    bool inside = (distance(sphere.xyz, Scene.Frustum.Sphere.xyz) < (sphere.w + Scene.Frustum.Sphere.w));
    return inside;
};

bool VisibleFrustumCone(float4 sphere)
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

bool VisibleFrustumConeAndNearPlane(float4 sphere) 
{
    bool i0 = VisibleFrustumCone(sphere);

    FrustumPlane frNear = Scene.Frustum.Planes[FRUSTUM_PLANE_NEAR];
    float d0 = SignedPointPlaneDistance(sphere.xyz, frNear.Normal, frNear.Position);
    bool  i1 = (abs(d0) < sphere.w); // Intersects with near plane
    bool  i2 = (d0 > 0);             // On positive half space of near plane

    return i0 && (i1 || i2);
};

[numthreads(AS_GROUP_SIZE, 1, 1)]
void asmain(
    uint gtid : SV_GroupThreadID,
    uint dtid : SV_DispatchThreadID,
    uint gid  : SV_GroupID
)
{
    bool visible = false;

    uint instanceIndex = dtid / Scene.MeshletCount;
    uint meshletIndex  = dtid % Scene.MeshletCount;

    // Make sure instance index is within bounds
    if (instanceIndex < Scene.InstanceCount) {
        // Instance's model transform matrix
        float4x4 M  = Instances[instanceIndex].M;

        // Assume LOD 0
        uint lod = 0;

        if (Scene.EnableLOD) {
            // Get center of transformed bounding box to use in LOD distance calculation
            float4 instanceBoundsMinWS = mul(M, float4(Scene.MeshBoundsMin, 1.0));
            float4 instanceBoundsMaxWS = mul(M, float4(Scene.MeshBoundsMax, 1.0));
            float4 instanceCenter = (instanceBoundsMinWS + instanceBoundsMaxWS) / 2.0;

            // Distance between transformed bounding box and camera eye position
            float dist = distance(instanceCenter.xyz, Scene.EyePosition);
            
            // Normalize distance using MaxLODDistance
            float ndist = clamp(dist / Scene.MaxLODDistance, 0.0, 1.0);
            
            // Calculate LOD using normalized distance
            lod = (uint)(pow(ndist, 0.65) * (MAX_LOD_COUNT - 1));
        }

        // Get meshlet count for the LOD
        uint lodMeshletCount = Scene.Meshlet_LOD_Counts[lod];

        // Make sure meshlet index is within bounds of current LOD's meshlet count
        if (meshletIndex < lodMeshletCount) {
            meshletIndex += Scene.Meshlet_LOD_Offsets[lod];

            // Transform meshlet's bounding sphere into world space
            float4 meshletBoundingSphere = mul(M, float4(MeshletBounds[meshletIndex].xyz, 1.0));
            meshletBoundingSphere.w = MeshletBounds[meshletIndex].w;
            
            if (Scene.VisibilityFunc == VISIBILITY_FUNC_NONE) {
                visible = true;
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_PLANES) {
                visible = VisibleFrustumPlanes(meshletBoundingSphere);
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_SPHERE) {
                visible = VisibleFrustumSphere(meshletBoundingSphere);
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_CONE) {
                visible = VisibleFrustumCone(meshletBoundingSphere);
            }
            else if (Scene.VisibilityFunc == VISIBILITY_FUNC_CONE_AND_NEAR_PLANE) {
                visible = VisibleFrustumConeAndNearPlane(meshletBoundingSphere);
            }
        }
    }

    if (visible) {
        uint index = WavePrefixCountBits(visible);
        sPayload.InstanceIndices[index] = instanceIndex;
        sPayload.MeshletIndices[index]  = meshletIndex;
    }
    
    uint visibleCount = WaveActiveCountBits(visible);    
    DispatchMesh(visibleCount, 1, 1, sPayload); 
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
    uint instanceIndex = payload.InstanceIndices[gid];
    uint meshletIndex  = payload.MeshletIndices[gid];

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
        uint packed = TriangleIndices[m.TriangleOffset + gtid];
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
