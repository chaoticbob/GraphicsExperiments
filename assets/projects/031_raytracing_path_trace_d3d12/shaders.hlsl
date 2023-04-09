#define PI          3.1415292
#define EPSILON     0.00001

// -----------------------------------------------------------------------------
// Common Resources
// -----------------------------------------------------------------------------
struct Light
{
    float3 Position;
    float3 Color;
    float  Intensity;
};

struct SceneParameters 
{
	float4x4 ViewInverseMatrix;
	float4x4 ProjectionInverseMatrix;
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;    
    uint     MaxSamples;
    uint     NumLights;
    Light    Lights[8];
};

ConstantBuffer<SceneParameters> SceneParams : register(b5); // Scene params

// -----------------------------------------------------------------------------
// Ray Tracing Resources
// -----------------------------------------------------------------------------

RaytracingAccelerationStructure  Scene         : register(t0); // Acceleration structure
RWTexture2D<float4>              RenderTarget  : register(u1); // Output texture
RWTexture2D<float4>              AccumTarget   : register(u2); // Accumulation texture
RWStructuredBuffer<uint>         RayGenSamples : register(u3); // Ray generation samples

struct Triangle {
    uint vIdx0;
    uint vIdx1;
    uint vIdx2;
};

StructuredBuffer<Triangle> Triangles[5] : register(t20); // Index buffer (4 spheres, 1 box)
StructuredBuffer<float3>   Positions[5] : register(t25); // Position buffer (4 spheres, 1 box)
StructuredBuffer<float3>   Normals[5]   : register(t30); // Normal buffer (4 spheres, 1 box)

// -----------------------------------------------------------------------------
// Material Parameters
// -----------------------------------------------------------------------------
struct MaterialParameters 
{
    float3 baseColor;
    float  roughness;
    float  metallic;
    float  specularReflectance;
    float  ior;
};

StructuredBuffer<MaterialParameters> MaterialParams : register(t9); // Material params

Texture2D    IBLEnvironmentMap : register(t100);
SamplerState IBLMapSampler     : register(s10);

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcg_hash(inout uint rngState)
{
    uint state = rngState;
    rngState = rngState * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random01(inout uint input)
{
    return pcg_hash(input) / 4294967296.0;
}

// -----------------------------------------------------------------------------
// Lighting Functions
// -----------------------------------------------------------------------------

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

float3 GetIBLEnvironment(float3 dir, float lod)
{
    float2 uv = CartesianToSpherical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);
    float3 color = IBLEnvironmentMap.SampleLevel(IBLMapSampler, uv, lod).rgb;
    color = min(color, (float3)100.0);
    return color;
}

// Samples hemisphere using Hammersley pattern for irradiance contribution
float3 GenIrradianceSampleDir(uint sampleIndex, float3 N)
{
    float2 Xi = Hammersley(sampleIndex, SceneParams.MaxSamples);
    float3 L  = ImportanceSampleGGX(Xi, 1.0, N);
    return L;
}

float3 GenSpecularSampleDir(uint sampleIndex, float3 N, float Roughness)
{
    float2 Xi = Hammersley(sampleIndex, SceneParams.MaxSamples);
    float3 L  = ImportanceSampleGGX(Xi, Roughness, N);
    return L;
}

// Samples hemisphere using RNG for irradiance contribution
float3 GenIrradianceSampleDirRNG(inout uint rngState, float3 N)
{
    float   u = Random01(rngState);
    float   v = Random01(rngState);
    float2 Xi = float2(u, v);
    float3 L  = ImportanceSampleGGX(Xi, 1.0, N);
    return L;
}

float3 GenSpecularSampleDirRNG(inout uint rngState, float3 N, float Roughness)
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

// -----------------------------------------------------------------------------
// Ray Tracing
// -----------------------------------------------------------------------------
typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct RayPayload
{
    float4 color;
    uint   rayDepth;
    uint   sampleIndex;
    uint   rngState;
};

[shader("raygeneration")]
void MyRaygenShader()
{
    uint2 rayIndex2 = DispatchRaysIndex().xy;
    uint  rayIndex = rayIndex2.y * 1920 + rayIndex2.x;
    uint  sampleCount = RayGenSamples[rayIndex];
    uint  rngState = sampleCount + rayIndex * 1943006372;
       
    if (sampleCount < SceneParams.MaxSamples)
    {
        float2 Xi = Hammersley(sampleCount, SceneParams.MaxSamples);
        const float2 pixelCenter = (float2) rayIndex2 + float2(0.5, 0.5) + Xi;
	    const float2 inUV = pixelCenter/(float2)DispatchRaysDimensions();
	    float2 d = inUV * 2.0 - 1.0;
        d.y = -d.y;

	    float4 origin = mul(SceneParams.ViewInverseMatrix, float4(0,0,0,1));
	    float4 target = mul(SceneParams.ProjectionInverseMatrix, float4(d.x, d.y, 1, 1));
	    float4 direction = mul(SceneParams.ViewInverseMatrix, float4(normalize(target.xyz), 0));

        RayDesc ray;
        ray.Origin = origin.xyz;
        ray.Direction = direction.xyz;
        ray.TMin = 0.001;
        ray.TMax = 10000.0;

        uint sampleIndex = (sampleCount + 15 * rayIndex) % SceneParams.MaxSamples;
        RayPayload payload = {float4(0, 0, 0, 0), 0, sampleCount, rngState };

        TraceRay(
            Scene,         // AccelerationStructure
            RAY_FLAG_NONE, // RayFlags
            ~0,            // InstanceInclusionMask
            0,             // RayContributionToHitGroupIndex
            1,             // MultiplierForGeometryContributionToHitGroupIndex
            0,             // MissShaderIndex
            ray,           // Ray
            payload);      // Payload
        
        sampleCount += 1;
        AccumTarget[rayIndex2] += payload.color;
    }
    
    float3 finalColor = AccumTarget[rayIndex2].xyz / (float)sampleCount;
    finalColor = ACESFilm(finalColor); 

    RenderTarget[rayIndex2] = float4(pow(finalColor, 1 / 2.2), 0);
    RayGenSamples[rayIndex] = sampleCount;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    float3 color = GetIBLEnvironment(dir, 0);
    payload.color = float4(color, 1);
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    uint instIdx = InstanceIndex();
    uint primIdx = PrimitiveIndex();
    Triangle tri = Triangles[instIdx][primIdx];

    float3 P = WorldRayOrigin() + (RayTCurrent() * WorldRayDirection());
    float3 I = normalize(WorldRayDirection());        
    float3 V = -I;
    bool   inside = (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE);       
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    
    float3 N0 = Normals[instIdx][tri.vIdx0];
    float3 N1 = Normals[instIdx][tri.vIdx1];
    float3 N2 = Normals[instIdx][tri.vIdx2];
    float3 N  = normalize(N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z);

    // Transform normal trom object to world space
    float3x4 m0 = ObjectToWorld3x4();
    float3x3 m1 = float3x3(m0._11, m0._12, m0._13,
                           m0._21, m0._22, m0._23,
                           m0._31, m0._32, m0._33);
    N = mul(m1, N);    

    // Material variables
    float3 baseColor = MaterialParams[instIdx].baseColor;
    float  roughness = MaterialParams[instIdx].roughness;
    float  metallic  = MaterialParams[instIdx].metallic;
    float  specularReflectance = MaterialParams[instIdx].specularReflectance;
    float  ior = MaterialParams[instIdx].ior;

    // Remap
    roughness = roughness * roughness;

    // Calculate F0
    float3 F0 = 0.16 * specularReflectance * specularReflectance * (1 - metallic) + baseColor * metallic;    
   
    float  cosTheta = saturate(dot(N, -I));
    float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    
    float3 reflection = 0;
    float3 refraction = 0;
    float  kr = 1;
    float  kt = 0;
    float  offset = 0.001;
    
    float eta1 = 1.0;
    float eta2 = ior;

    if (inside) {
        float temp = eta1;
        eta1 = eta2;
        eta2 = temp;
        N = -N;
    }

    if (ior > 1.0) {
        kr = saturate(FresnelSchlickReflectionAmount(I, N, eta1, eta2)); 
        kt = 1.0 - kr;
    }
    
    if (payload.rayDepth < 7) {
        if (kr > 0) {
            // Diffuse
            {                
            //float3 L = GenIrradianceSampleDir(payload.sampleIndex, N);
            float3 L = GenIrradianceSampleDirRNG(payload.rngState, N);

                float3 rayDir = L;
        
                RayDesc ray;
                ray.Origin = P + offset * rayDir;
                ray.Direction = rayDir;
                ray.TMin = 0.001;
                ray.TMax = 10000.0;
        
                RayPayload thisPayload = {(float4)0, payload.rayDepth + 1, payload.sampleIndex, payload.rngState};
       
                TraceRay(
                    Scene,                  // AccelerationStructure
                    RAY_FLAG_FORCE_OPAQUE,  // RayFlags
                    ~0,                     // InstanceInclusionMask
                    0,                      // RayContributionToHitGroupIndex
                    1,                      // MultiplierForGeometryContributionToHitGroupIndex
                    0,                      // MissShaderIndex
                    ray,                    // Ray
                    thisPayload);           // Payload
        
                float3 bounceColor = thisPayload.color.xyz;
                payload.rngState = thisPayload.rngState;

                float NoL = saturate(dot(N, L));
                reflection += kD * bounceColor * NoL;                
            }
        
            // Specular
            {
            //float3 H = normalize(GenSpecularSampleDir(payload.sampleIndex, N, roughness));
            float3 H = normalize(GenSpecularSampleDirRNG(payload.rngState, N, roughness));
            float3 L = 2.0 * dot(V, H) * H  - V;
                
                float3 rayDir = L;
        
                RayDesc ray;
                ray.Origin = P + offset * rayDir;
                ray.Direction = rayDir;
                ray.TMin = 0.001;
                ray.TMax = 10000.0;
        
                RayPayload thisPayload = {(float4)0, payload.rayDepth + 1, payload.sampleIndex, payload.rngState};
       
                TraceRay(
                    Scene,                  // AccelerationStructure
                    RAY_FLAG_FORCE_OPAQUE,  // RayFlags
                    ~0,                     // InstanceInclusionMask
                    0,                      // RayContributionToHitGroupIndex
                    1,                      // MultiplierForGeometryContributionToHitGroupIndex
                    0,                      // MissShaderIndex
                    ray,                    // Ray
                    thisPayload);           // Payload
                                      
                float3 bounceColor = thisPayload.color.xyz;
                payload.rngState = thisPayload.rngState;

                reflection += F * bounceColor;
            }
        }
                
        // Refraction
        if (kt > 0) {
            float3 rayDir = refract(I, N, eta1 / eta2);
            
            RayDesc ray;
            ray.Origin = P + offset * rayDir;
            ray.Direction = rayDir;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;
            
            RayPayload thisPayload = {(float4)0, payload.rayDepth + 1, payload.sampleIndex, payload.rngState};
            
            TraceRay(
            Scene,          // AccelerationStructure
            RAY_FLAG_NONE,  // RayFlags
            ~0,             // InstanceInclusionMask
            0,              // RayContributionToHitGroupIndex
            0,              // MultiplierForGeometryContributionToHitGroupIndex
            0,              // MissShaderIndex
            ray,            // Ray
            thisPayload);   // Payload   
    
            float3 bounceColor = thisPayload.color.xyz;
            payload.rngState = thisPayload.rngState;

            refraction += bounceColor;          
        }
    }
    
    float3 finalColor = (reflection * kr * baseColor) + (refraction * kt);
    
    payload.color = float4(finalColor, 0);
}
