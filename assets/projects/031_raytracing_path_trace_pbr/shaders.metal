#include <metal_stdlib>

using namespace metal;
using namespace raytracing;

#define PI                  3.1415292
#define EPSILON             0.00001
#define MAX_RAY_DEPTH_LIMIT 32

// -------------------------------------------------------------------------------------------------
// Common Resources
// -------------------------------------------------------------------------------------------------
struct Light
{
    packed_float3 Position;
    uint          _pad0;
    packed_float3 Color;
    float         Intensity;
};

struct SceneParameters 
{
	float4x4      ViewInverseMatrix;
	float4x4      ProjectionInverseMatrix;
	float4x4      ViewProjectionMatrix;
    packed_float3 EyePosition;     
    uint          IBLIndex;   
    uint          MaxSamples;
    uint          NumLights;
    uint          _pad0[2];
    Light         Lights[8];
};

// -------------------------------------------------------------------------------------------------
// Ray Tracing Resources
// -------------------------------------------------------------------------------------------------
struct Triangle {
    uint vIdx0;
    uint vIdx1;
    uint vIdx2;
};

struct GeometryBuffers {
    array<device const Triangle*, 25>      Triangles [[id(0)]];  // Triangle indices
    array<device const packed_float3*, 25> Positions [[id(25)]]; // Vertex positions
    array<device const packed_float3*, 25> Normals   [[id(50)]]; // Vertex normals
};

struct IBLTextures {
    array<texture2d<float>, 100> EnvironmentMaps[[id(0)]];
};

// -------------------------------------------------------------------------------------------------
// Material Parameters
// -------------------------------------------------------------------------------------------------
struct MaterialParameters 
{
    packed_float3 baseColor;
    float         roughness;
    float         metallic;
    float         specularReflectance;
    float         ior;
    uint          _pad0;
    packed_float3 emissionColor;
    uint          _pad1;
};

constexpr sampler IBLMapSampler(
    filter::linear,
    mip_filter::linear,
    s_address::repeat);

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcg_hash(thread uint& rngState)
{
    uint state = rngState;
    rngState = rngState * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random01(thread uint& input)
{
    return pcg_hash(input) / 4294967296.0;
}

// -------------------------------------------------------------------------------------------------
// Lighting Functions
// -------------------------------------------------------------------------------------------------
float Geometry_SchlickBeckman(float NoV, float k)
{
	return NoV / (NoV * (1 - k) + k);
}

float Geometry_Smith(float3 N, float3 V, float3 L,  float roughness)
{    
    float k   = pow(roughness + 1, 2) / 8.0; 
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));    
    float G1  = Geometry_SchlickBeckman(NoV, k);
    float G2  = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
}

// Geometry for IBL uses a different k than direct lighting
float Geometry_SmithIBL(float3 N, float3 V, float3 L,  float roughness)
{
    float k = (roughness * roughness) / 2.0;
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));    
    float G1 = Geometry_SchlickBeckman(NoV, k);
    float G2 = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
}

float3 Fresnel_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = (float3)(1 - roughness);
    return F0 + (max(r, F0) - F0) * pow(1 - cosTheta, 5);
}

float FresnelSchlickReflectionAmount(float3 I, float3 N, float n1, float n2)
{
    float r0 = (n1 - n2) / (n1 + n2);
    r0 = r0 * r0;
    
    float cosX = -dot(I, N);
    if (n1 > n2) {
        float n = n1 / n2;
        float sinT2 = n * n * (1.0 - cosX * cosX);
        if (sinT2 > 1.0) {
            return 1.0; // TIR
        }
        cosX = sqrt(1.0 - sinT2);
    }
    float x = 1.0 - cosX;
    float fr = r0 + (1.0 - r0) * x * x * x * x *x;

    return fr;
}

// circular atan2 - converts (x,y) on a unit circle to [0, 2pi]
//
#define catan2_epsilon 0.00001
#define catan2_NAN     0.0 / 0.0 // No gaurantee this is correct

float catan2(float y, float x)
{ 
    float absx = abs(x);
    float absy = abs(y);
    if ((absx < catan2_epsilon) && (absy < catan2_epsilon)) {
        return catan2_NAN;
    }
    else if ((absx > 0) && (absy == 0.0)) {
        return 0.0;
    }
    float s = 1.5 * 3.141592;
    if (y >= 0) {
        s = 3.141592 / 2.0;
    }
    return s - atan(x / y);
}

// Converts cartesian unit position 'pos' to (theta, phi) in
// spherical coodinates.
//
// theta is the azimuth angle between [0, 2pi].
// phi is the polar angle between [0, pi].
//
// NOTE: (0, 0, 0) will result in nan
//
float2 CartesianToSpherical(float3 pos)
{
    float absX = abs(pos.x);
    float absZ = abs(pos.z);
    // Handle pos pointing straight up or straight down
    if ((absX < 0.00001) && (absZ <= 0.00001)) {
        // Pointing straight up
        if (pos.y > 0) {
            return float2(0, 0);
        }
        // Pointing straight down
        else if (pos.y < 0) {
            return float2(0, 3.141592);
        }
        // Something went terribly wrong
        else {            
            return float2(catan2_NAN, catan2_NAN);
        }
    }
    float theta = catan2(pos.z, pos.x);
    float phi   = acos(pos.y);
    return float2(theta, phi);
}

float2 Hammersley(uint i, uint N)
{
    uint bits = (i << 16u) | (i >> 16u);
    bits      = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits      = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits      = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits      = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rdi = float(bits) * 2.3283064365386963e-10f;
    return float2(float(i) / float(N), rdi);
}

float3 ImportanceSampleGGX(float2 Xi, float Roughness, float3 N)
{
    float a        = Roughness * Roughness;
    float Phi      = 2 * PI * Xi.x;
    float CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
    float SinTheta = sqrt(1 - CosTheta * CosTheta);

    float3 H        = (float3)0;
    H.x             = SinTheta * cos(Phi);
    H.y             = SinTheta * sin(Phi);
    H.z             = CosTheta;
    float3 UpVector = abs(N.y) < 0.99999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 TangentX = normalize(cross(UpVector, N));
    float3 TangentY = cross(N, TangentX);

    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

float3 GetIBLEnvironment(
    float3                        dir, 
    float                         lod, 
    device const SceneParameters& SceneParams, 
    device IBLTextures*           IBL)
{
    float2 uv = CartesianToSpherical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);
    float3 color = IBL->EnvironmentMaps[SceneParams.IBLIndex].sample(IBLMapSampler, uv, level(lod)).rgb;
    color = min(color, (float3)100.0);
    return color;
}

// Samples hemisphere using RNG for irradiance contribution
float3 GenIrradianceSampleDirRNG(thread uint& rngState, float3 N)
{
    float   u = Random01(rngState);
    float   v = Random01(rngState);
    float2 Xi = float2(u, v);
    float3 L  = ImportanceSampleGGX(Xi, 1.0, N);
    return L;
}

float3 GenSpecularSampleDirRNG(thread uint& rngState, float3 N, float Roughness)
{
    float   u = Random01(rngState);
    float   v = Random01(rngState);
    float2 Xi = float2(u, v);
    float3 L  = ImportanceSampleGGX(Xi, Roughness, N);
    return L;
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// =================================================================================================
// Ray trace shader
// =================================================================================================
struct RayPayload
{
    float4 color;
    uint   rayDepth;
    uint   sampleIndex;
    uint   rngState;
};

struct HitInfo
{
    float3 P;
    float3 N;
    uint   materialIndex;
    bool   inside;
};

enum RayType {
    RAY_TYPE_PRIMARY  = 0,
    RAY_TYPE_DIFFUSE  = 1,
    RAY_TYPE_SPECULAR = 2,
    RAY_TYPE_REFRACT  = 3,
};

typedef intersector<triangle_data, instancing>              intersector_type;
typedef intersector<triangle_data, instancing>::result_type intersector_result_type;

HitInfo GetHitInfo(
    device MTLAccelerationStructureInstanceDescriptor*  Instances,
    device GeometryBuffers&                             Geometry,
    thread intersector_result_type                      intersection,
    ray                                                 worldRay);     

//
// The Metal version of this sample is slightly different from the D3D12
// and Vulkan versions. Metal doesn't support recursive ray tracing so the 
// an iterative approach is used instead. This approach produces results
// similar to but not exactly like the D3D12 and Vulkan versions. Thankfully
// the samples are just to demonstrate a technique of path tracing on the 
// GPU without a high degree of emphasis on correctness.
//
kernel void MyRayGen(
    uint2                                                DispatchRaysIndex      [[thread_position_in_grid]],
    uint2                                                DispatchRaysDimensions [[threads_per_grid]],
    instance_acceleration_structure                      Scene                  [[buffer(0)]],
    device MTLAccelerationStructureInstanceDescriptor*   Instances              [[buffer(1)]],
    device const SceneParameters&                        SceneParams            [[buffer(2)]],
    device GeometryBuffers&                              Geometry               [[buffer(6)]],
    device const MaterialParameters*                     MaterialParams         [[buffer(4)]],
    device uint*                                         RayGenSamples          [[buffer(5)]],
    device IBLTextures*                                  IBL                    [[buffer(7)]],
    texture2d<float, access::write>                      RenderTarget           [[texture(0)]],
    texture2d<float, access::read_write>                 AccumTarget            [[texture(1)]])
{
    uint2 rayIndex2 = DispatchRaysIndex.xy;
    uint  rayIndex = rayIndex2.y * DispatchRaysDimensions.x + rayIndex2.x;
    uint  sampleCount = RayGenSamples[rayIndex];
    uint  rngState = sampleCount + rayIndex * 1943006372;

    if (sampleCount < SceneParams.MaxSamples)
    {
        float2 Xi = Hammersley(sampleCount, SceneParams.MaxSamples);
        const float2 pixelCenter = (float2) rayIndex2 + float2(0.5, 0.5) + Xi;
        const float2 inUV = pixelCenter/(float2)DispatchRaysDimensions;
        float2 d = inUV * 2.0 - 1.0;
        d.y = -d.y;

        float3  rayOrigin    = (SceneParams.ViewInverseMatrix * float4(0,0,0,1)).xyz;
        float3  rayTarget    = (SceneParams.ProjectionInverseMatrix * float4(d.x, d.y, 1, 1)).xyz;
        float3  rayDirection = (SceneParams.ViewInverseMatrix * float4(normalize(rayTarget.xyz), 0)).xyz;
        RayType rayType      = RAY_TYPE_PRIMARY;

        // Max bounces (aka ray depth)
        const uint  kRayDepthLimit = min(10, MAX_RAY_DEPTH_LIMIT);
        const float kTraceOffset   = 0.001;

        // Triange intersector
        intersector_type primitiveIntersector;

        float3 throughPut = 1.0;
        float3 color = 0.0;
        for (uint rayDepth = 0; rayDepth < kRayDepthLimit; ++rayDepth) {
            ray worldRay;
            worldRay.origin       = rayOrigin;
            worldRay.direction    = rayDirection;
            worldRay.min_distance = 0.001;
            worldRay.max_distance = 10000.0;  

            intersector_result_type intersection = primitiveIntersector.intersect(worldRay, Scene, 1);

            if (intersection.type == intersection_type::triangle) {
                // *** CLOSEST HIT ***

                // Get hit info
                HitInfo hit = GetHitInfo(Instances, Geometry, intersection, worldRay);
                // Convenience vars
                float3 N = hit.N;
                float3 I = worldRay.direction;
                float3 V = -I;
        
                // Material
                MaterialParameters material = MaterialParams[hit.materialIndex];
                float3 baseColor = material.baseColor;
                float  roughness = material.roughness;
                float  metallic  = material.metallic;
                float  specularReflectance =material.specularReflectance;
                float  ior = material.ior;
                 float3 emission = material.emissionColor;
                
                // Remap roughness
                roughness = material.roughness * material.roughness;            
                
                // Calculate F0
                float3 F0 = 0.16 * specularReflectance * specularReflectance * (1 - metallic) + baseColor * metallic;                

                // Refraction
                float eta1 = 1.0;
                float eta2 = ior;
                if (hit.inside) {
                    float temp = eta1;
                    eta1 = eta2;
                    eta2 = temp;
                    N = -N;
                }
                float kr = 1.0;
                float kt = 0;
                if (ior > 1.0) {
                    kr = saturate(FresnelSchlickReflectionAmount(I, N, eta1, eta2)); 
                    kt = 1.0 - kr;
                }

                float diceRoll = Random01(rngState);
                if (ior > 1.0) {
                    rayType = (diceRoll > 0.66) ? RAY_TYPE_REFRACT : ((diceRoll > 0.33) ? RAY_TYPE_SPECULAR : RAY_TYPE_DIFFUSE);
                }
                else {
                    rayType = (diceRoll > 0.5) ? RAY_TYPE_SPECULAR : RAY_TYPE_DIFFUSE;
                }

                if (rayType == RAY_TYPE_DIFFUSE) {
                    float  NoV = saturate(dot(N, V));
                    float3 F = Fresnel_SchlickRoughness(NoV, F0, roughness);
                    float3 kD = (1.0 - F) * (1.0 - metallic);

                    float3 L = normalize(GenIrradianceSampleDirRNG(rngState, N));
                    float NoL = saturate(dot(N, L));

                    throughPut *= (ior > 1.0) ? (kD * NoL * kr) : (kD * NoL * baseColor);
                    color += throughPut;

                    // Update ray direction
                    rayDirection = L;
                }
                else if (rayType == RAY_TYPE_SPECULAR) {
                    //
                    // Most of the logic for specular contribution was understood from here:
                    //   https://www.mathematik.uni-marburg.de/~thormae/lectures/graphics1/graphics_10_2_eng_web.html#1
                    //

                    float3 H = normalize(GenSpecularSampleDirRNG(rngState, N, roughness));
                    float3 L = 2.0 * dot(V, H) * H  - V;

                    float NoV = saturate(dot(N, V));
                    float NoL = saturate(dot(N, L));
                    float NoH = saturate(dot(N, H));
                    float VoH = saturate(dot(V, H));

                    float3 F = Fresnel_SchlickRoughness(VoH, F0, roughness);
                    float  G = Geometry_SmithIBL(N, V, L, roughness);

                    if (ior > 1.0) {
                        throughPut *= F * kr;
                        color *= throughPut;                        
                    } 
                    else {
                        if ((NoL > 0) && (NoH > 0) && (NoV > 0) && (VoH > 0)) {
                            throughPut *= (F * G * VoH / (NoH * NoV) * baseColor) * kr;
                            color *= throughPut;
                        }
                    }
                 
                    // Update ray direction
                    rayDirection = L;
                }
                else if (rayType == RAY_TYPE_REFRACT) {
                    throughPut += (kt * diceRoll);
                    color *= throughPut;

                    // Update ray direction
                    rayDirection = refract(I, N, eta1 / eta2);
                }

                // HACK: give non-refractive surfaces a GI color bleed boost
                if (ior <= 1.0) {
                    throughPut += 0.15 * baseColor;
                }

                // Add emission
                throughPut += emission;

                // Update rayOrigin and rayDirection in case there's another bounce
                rayOrigin = hit.P + (kTraceOffset * rayDirection);
            }
            else if (intersection.type == intersection_type::none) {
                // *** MISS ***            
                float3 envColor = GetIBLEnvironment(worldRay.direction, 0, SceneParams, IBL);
                color += throughPut *  envColor;
                break;
            }
        }

        sampleCount += 1;
        float4 accumColor = AccumTarget.read(rayIndex2) + float4(color, 1);
        AccumTarget.write(accumColor, rayIndex2);        
    }

    float4 accumColor = AccumTarget.read(rayIndex2);
    float3 finalColor = accumColor.xyz / (float)sampleCount;
    finalColor = ACESFilm(finalColor);     

    // Readjusting gamma on output makes the image appear to too bright, disable for now.
    //RenderTarget.write(float4(pow(finalColor, 1 / 1.0), 0), rayIndex2);
    //
    RenderTarget.write(float4(finalColor, 0), rayIndex2);
    RayGenSamples[rayIndex] = sampleCount;
}

HitInfo GetHitInfo(
    device MTLAccelerationStructureInstanceDescriptor*  Instances,
    device GeometryBuffers&                             Geometry,
    thread intersector_result_type                      intersection,
    ray                                                 worldRay)
{
    uint instIdx = intersection.instance_id;
    uint primIdx = intersection.primitive_id;
    Triangle tri = Geometry.Triangles[instIdx][primIdx];

    float3 P = worldRay.origin + intersection.distance * worldRay.direction;
    bool   inside = (intersection.triangle_front_facing == false);
    float3 barycentrics = float3(
        1 - intersection.triangle_barycentric_coord.x - intersection.triangle_barycentric_coord.y,
        intersection.triangle_barycentric_coord.x,
        intersection.triangle_barycentric_coord.y);

    float3 N0 = Geometry.Normals[instIdx][tri.vIdx0];
    float3 N1 = Geometry.Normals[instIdx][tri.vIdx1];
    float3 N2 = Geometry.Normals[instIdx][tri.vIdx2];
    float3 N  = normalize(N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z);

    // Get object to world space...the Metal way!
    float4x4 objectToWorld(1.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 3; ++row) {
            objectToWorld[column][row] = Instances[instIdx].transformationMatrix[column][row];
        }
    }
    
    // Transform normal from object to world space
    float3x3 m = float3x3(
        objectToWorld[0].xyz, 
        objectToWorld[1].xyz, 
        objectToWorld[2].xyz);
    N = m * N;

    HitInfo hit = {P, N, instIdx, inside};
    return hit;
}

// =================================================================================================
// Clear ray gen buffers
// =================================================================================================
kernel void Clear(
    uint2                           dtid          [[thread_position_in_grid]],
    texture2d<float, access::write> AccumTarget   [[texture(0)]],
    device uint*                    RayGenSamples [[buffer(0)]])
{
    AccumTarget.write(float4(0, 0, 0, 0), dtid);

    uint idx = dtid.y * 1280 + dtid.x;
    RayGenSamples[idx] = 0;
}

// =================================================================================================
// VS and PS is used to copy ray traced image to swapchain
// =================================================================================================

struct VSOutput {
    float4 Position [[position]];
    float2 TexCoord;
};

vertex VSOutput vsmain(unsigned short id [[vertex_id]])
{
    VSOutput result;

    // Clip space position
    result.Position.x = (float)(id / 2) * 4.0 - 1.0;
    result.Position.y = (float)(id % 2) * 4.0 - 1.0;
    result.Position.z = 0.0;
    result.Position.w = 1.0;

    // Texture coordinates
    result.TexCoord.x = (float)(id / 2) * 2.0;
    result.TexCoord.y = 1.0 - (float)(id % 2) * 2.0;

    return result;
}

fragment float4 psmain(VSOutput input [[stage_in]], texture2d<float> Tex0)
{
    constexpr sampler Sampler0(min_filter::nearest, mag_filter::nearest, mip_filter::none);
    return Tex0.sample(Sampler0, input.TexCoord);
}
