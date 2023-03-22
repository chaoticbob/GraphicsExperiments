#define PI      3.1415292
#define EPSILON 0.00001

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
    uint     NumLights;
    Light    Lights[8];
};

ConstantBuffer<SceneParameters> SceneParams : register(b5); // Scene params

// Since we can't easily transform the spheres individually in this
// version - we'll just apply the rotation to the camera rays. The transformed
// ray and origin will affect the entire acceleration structure.
//
struct ModelParameters 
{
    float4x4 ModelMatrix;
    float4x4 InverseModelMatrix;
};

ConstantBuffer<ModelParameters> ModelParams : register(b6); // Model params

// -----------------------------------------------------------------------------
// Ray Tracing Resources
// -----------------------------------------------------------------------------

RaytracingAccelerationStructure  Scene         : register(t0); // Acceleration structure
RWTexture2D<float4>              RenderTarget  : register(u1); // Output texture
RWStructuredBuffer<uint>         RayGenSamples : register(u2); // Ray generation samples

struct Triangle {
    uint vIdx0;
    uint vIdx1;
    uint vIdx2;
};

StructuredBuffer<Triangle> Triangles[5] : register(t20); // Index buffer (4 spheres, 1 box)
StructuredBuffer<float3>   Positions[5] : register(t25); // Position buffer (4 spheres, 1 box)
StructuredBuffer<float3>   Normals[5]   : register(t30); // Normal buffer (4 spheres, 1 box)

// -----------------------------------------------------------------------------
// PBR Resources
// -----------------------------------------------------------------------------
struct MaterialParameters 
{
    float3 baseColor;
    float  roughness;
    float  metallic;
    float  specularReflectance;
};

// We'll use a StructuredBuffer here version since we can't 
// set root constants per draw. 
//
StructuredBuffer<MaterialParameters> MaterialParams : register(t9); // Material params

Texture2D    IBLIntegrationLUT     : register(t10);
Texture2D    IBLIrradianceMap      : register(t11);
Texture2D    IBLEnvironmentMap     : register(t12);
SamplerState IBLIntegrationSampler : register(s13);
SamplerState IBLMapSampler         : register(s14);

// -----------------------------------------------------------------------------
// PBR Functions
// -----------------------------------------------------------------------------

float Distribution_GGX(float3 N, float3 H, float roughness)
{
    float NoH    = saturate(dot(N, H));
    float NoH2   = NoH * NoH;
    float alpha2 = max(roughness * roughness, EPSILON);
    float A      = NoH2 * (alpha2 - 1) + 1;
	return alpha2 / (PI * A * A);
}

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

float3 Fresnel_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
} 

float3 Fresnel_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = (float3)(1 - roughness);
    return F0 + (max(r, F0) - F0) * pow(1 - cosTheta, 5);
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
    float3 UpVector = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
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
    return color;
}

// Samples hemisphere using Hammersley pattern for irradiance contribution
float3 GetIBLIrradiance(float3 N)
{
    const float a = 1.0;
    float3 Color = 0;

    const uint NumSamples = 1024;
    for (uint i = 0; i < NumSamples; ++i) {
        float2 Xi       = Hammersley(i, NumSamples);
        float  Phi      = 2 * PI * Xi.x;
        //float  CosTheta = cos(PI / 2 * Xi.y);
        float CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
        float  SinTheta = sqrt(1 - CosTheta * CosTheta);

        float3 L        = (float3)0;
        L.x             = SinTheta * cos(Phi);
        L.y             = SinTheta * sin(Phi);
        L.z             = CosTheta;
        float3 UpVector = abs(N.y) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
        float3 TangentX = normalize(cross(UpVector, N));
        float3 TangentY = cross(N, TangentX);

        // Tangent to world space
        L = TangentX * L.x + TangentY * L.y + N * L.z;

        float3 SampleColor = GetIBLEnvironment(L, 0);
        Color += SampleColor;
    }

    return Color / NumSamples;
}

// Importance samples the hemisphere for specular contribution
float3 GetIBLSpecular(float3 SpecularColor, float3 N, float3 V, float Roughness)
{
    float3 Color = 0;

    const uint NumSamples = 1024;
    for (uint i = 0; i < NumSamples; ++i) {
        float2 Xi = Hammersley(i, NumSamples);
        float3 H   = ImportanceSampleGGX(Xi, Roughness, N);
        float3 L   = 2 * dot(V, H) * H - V;

        float NoV = saturate(dot(N , V));
        float NoL = saturate(dot(N , L));
        float NoH = saturate(dot(N , H));
        float VoH = saturate(dot(V , H));

        if (NoL > 0) {
            float3 SampleColor = GetIBLEnvironment(L, 0);

            float  G  = Geometry_SmithIBL(N, V, L, Roughness);
            float  Fc = pow(1 - VoH, 5);
            float3 F  = (1 - Fc) * SpecularColor + Fc;

            Color += SampleColor * F * G * VoH / (NoH * NoV);
        }
    }

    return Color / NumSamples;
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
};

[shader("raygeneration")]
void MyRaygenShader()
{
	const float2 pixelCenter = (float2)DispatchRaysIndex() + float2(0.5, 0.5);
	const float2 inUV = pixelCenter/(float2)DispatchRaysDimensions();
	float2 d = inUV * 2.0 - 1.0;
    d.y = -d.y;

	float4 origin = mul(SceneParams.ViewInverseMatrix, float4(0,0,0,1));
	float4 target = mul(SceneParams.ProjectionInverseMatrix, float4(d.x, d.y, 1, 1));
	float4 direction = mul(SceneParams.ViewInverseMatrix, float4(normalize(target.xyz), 0));

    // Apply the model transformation to the ray's origin and direction
    // effectively transforming the geometry in the acceleration structure.
    //
    // NOTE: This transforms the ray from world space into object space.
    //
    origin = mul(ModelParams.ModelMatrix, origin);
    direction = mul(ModelParams.ModelMatrix, direction);

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = direction.xyz;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {float4(0, 0, 0, 0)};

    TraceRay(
        Scene,                 // AccelerationStructure
        RAY_FLAG_FORCE_OPAQUE, // RayFlags
        ~0,                    // InstanceInclusionMask
        0,                     // RayContributionToHitGroupIndex
        1,                     // MultiplierForGeometryContributionToHitGroupIndex
        0,                     // MissShaderIndex
        ray,                   // Ray
        payload);              // Payload

    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    
    // Since we only want to transform the geometry - we'll undo the model transform.
    // NOTE: This transforms the ray direction from object space to world space.
    //
    dir = mul(ModelParams.InverseModelMatrix, float4(dir, 0)).xyz;

    float3 color = GetIBLEnvironment(dir, 0);
    color = ACESFilm(color);
    color = pow(color, 0.9);
    payload.color = float4(color, 1);
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    uint instIdx = InstanceIndex();
    uint primIdx = PrimitiveIndex();
    Triangle tri = Triangles[instIdx][primIdx];

    float3 hitPosition  = WorldRayOrigin() + (RayTCurrent() * WorldRayDirection());
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    float3 N0 = Normals[instIdx][tri.vIdx0];
    float3 N1 = Normals[instIdx][tri.vIdx1];
    float3 N2 = Normals[instIdx][tri.vIdx2];
    float3 N  = N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z;
    
    // Since the ray was transformed into object space during ray generation
    // and the intersection is in object space - we'll need to transform back
    // to world space for lighting.
    // 
    // NOTE: This transforms P and N from object space to world space.
    //
    float3 P = mul(ModelParams.InverseModelMatrix, float4(hitPosition, 1)).xyz;
           N = mul(ModelParams.InverseModelMatrix, float4(N, 0)).xyz;
    float3 V = normalize(SceneParams.EyePosition - P);
    float3 R = reflect(-V, N);

    // -------------------------------------------------------------------------
    // Where the PBR begins...
    // -------------------------------------------------------------------------

    // Material variables
    float3 baseColor = MaterialParams[instIdx].baseColor;
    float  roughness = MaterialParams[instIdx].roughness;
    float  metallic  = MaterialParams[instIdx].metallic;
    float  specularReflectance = MaterialParams[instIdx].specularReflectance;

    // Remap
    roughness = roughness * roughness;

    // Calculate F0
    float3 F0 = 0.16 * specularReflectance * specularReflectance * (1 - metallic) + baseColor * metallic;    

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float cosTheta = saturate(dot(N, V));

        // Diffuse IBL component
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 irradiance = GetIBLIrradiance(R);
        float3 diffuse = irradiance * baseColor / PI;

        // Specular IBL component
        float3 specularColor = float3(1, 1, 1);
        float3 specular = GetIBLSpecular(specularColor, N, V, roughness);

        indirectLighting = kD * diffuse; // + specular;
    }

    // Final color
    float3 finalColor = indirectLighting;

    finalColor = ACESFilm(finalColor);      
    payload.color = float4(pow(finalColor, 1 / 2.2), 0);
}
