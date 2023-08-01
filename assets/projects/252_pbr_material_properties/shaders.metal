#include <metal_stdlib>
using namespace metal;

#define PI      3.1415292
#define EPSILON 0.000001

struct Light
{
    float3 Position;
    float3 Color;
    float  Intensity;
};

struct SceneParameters
{
    float4x4 ViewProjectionMatrix;
    float3   EyePosition;
    uint     NumLights;
    Light    Lights[8];
    uint     IBLEnvironmentNumLevels;
    uint     Multiscatter;
    uint     Furnace;
};

struct DrawParameters
{
    float4x4 ModelMatrix;
};

struct MaterialParameters
{
    float3 BaseColor;
    float  Roughness;
    float  Metallic;
    float  Reflectance;
    float  ClearCoat;
    float  ClearCoatRoughness;
    float  Anisotropy;
};

// =================================================================================================
// Vertex Shader
// =================================================================================================
struct VSInput
{
    float3 PositionOS [[attribute(0)]];
    float3 Normal     [[attribute(1)]];
    float3 Tangent    [[attribute(2)]];
    float3 Bitangent  [[attribute(3)]];
};

struct VSOutput
{
    float3 PositionWS;
    float4 PositionCS [[position]];
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
};

VSOutput vertex vsmain(
             VSInput          vertexData  [[stage_in]],
    constant SceneParameters& SceneParams [[buffer(4)]],
    constant DrawParameters&  DrawParams  [[buffer(5)]])
{
    VSOutput output;
    output.PositionWS = (DrawParams.ModelMatrix * float4(vertexData.PositionOS, 1)).xyz;
    output.PositionCS = (SceneParams.ViewProjectionMatrix * float4(output.PositionWS, 1));
    output.Normal     = (DrawParams.ModelMatrix * float4(vertexData.Normal, 0)).xyz;
    output.Tangent    = (DrawParams.ModelMatrix * float4(vertexData.Tangent, 0)).xyz;
    output.Bitangent  = (DrawParams.ModelMatrix * float4(vertexData.Bitangent, 0)).xyz;

    return output;
}

// =================================================================================================
// Pixel Shader
//
// PBR adapted from https://www.shadertoy.com/view/3tlBW7
//
// =================================================================================================

constexpr sampler IBLIntegrationSampler(
    filter::linear,
    mip_filter::linear);

constexpr sampler IBLMapSampler(
    filter::linear,
    mip_filter::linear,
    s_address::repeat);

float Distribution_GGX(float3 N, float3 H, float roughness)
{
    float NoH    = saturate(dot(N, H));
    float NoH2   = NoH * NoH;
    float alpha2 = max(roughness * roughness, EPSILON);
    float A      = NoH2 * (alpha2 - 1) + 1;
    return alpha2 / (PI * A * A);
}

// Anisotropic distribution and visibility functions from Filament
float DistributionAnisotropic_GGX(float NoH, float3 H, float3 T, float3 B, float at, float ab)
{
    float  ToH = dot(T, H);
    float  BoH = dot(B, H);
    float  a2  = at * ab;
    float3 v   = float3(ab * ToH, at * BoH, a2 * NoH);
    float  v2  = dot(v, v);
    float  w2  = a2 / v2;
    return a2 * w2 * w2 * (1.0 / PI);
}

float Geometry_SchlickBeckman(float NoV, float k)
{
    return NoV / (NoV * (1 - k) + k);
}

float Geometry_Smith(float3 N, float3 V, float3 L, float roughness)
{
    float k   = pow(roughness + 1, 2) / 8.0;
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));
    float G1  = Geometry_SchlickBeckman(NoV, k);
    float G2  = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
}

// Smiths GGX correlated anisotropic
float GeometryAnisotropic_SmithsGGX(
    float at,
    float ab,
    float ToV,
    float BoV,
    float ToL,
    float BoL,
    float NoV,
    float NoL)
{
    float lambdaV = NoV * length(float3(at * ToV, ab * BoV, NoV));
    float lambdaL = NoL * length(float3(at * ToL, ab * BoL, NoL));
    float v       = 0.5 / (lambdaV + lambdaL);
    return saturate(v);
}

float3 Fresnel_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = (float3)(1 - roughness);
    return F0 + (max(r, F0) - F0) * pow(1 - cosTheta, 5);
}

float3 Fresnel_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1 - F0) * pow(1 - cosTheta, 5);
}

float F_Schlick90(float cosTheta, float F0, float F90)
{
    return F0 + (F90 - F0) * pow(1.0 - cosTheta, 5.0);
}

float Fd_Lambert()
{
    return 1.0 / PI;
}

float Fd_Burley(float NoV, float NoL, float LoH, float roughness)
{
    float f90          = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick90(NoL, 1.0, f90);
    float viewScatter  = F_Schlick90(NoV, 1.0, f90);
    return lightScatter * viewScatter * (1.0 / PI);
}

//
// https://google.github.io/filament/Filament.md.html#lighting/imagebasedlights/anisotropy
//
float3 GetReflectedVector(float3 V, float3 N, float3 T, float3 B, float roughness, float anisotropy)
{
    float3 anisotropyDirection = anisotropy >= 0.0 ? B : T;
    float3 anisotropicTangent  = cross(anisotropyDirection, V);
    float3 anisotropicNormal   = cross(anisotropicTangent, anisotropyDirection);
    float3 bentNormal          = normalize(mix(N, anisotropicNormal, abs(anisotropy)));
    return reflect(-V, bentNormal);
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
float2 CartesianToSphereical(float3 pos)
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

float3 GetIBLIrradiance(texture2d<float> IBLIrradianceMap, float3 dir)
{
    float2 uv    = CartesianToSphereical(normalize(dir));
    uv.x         = saturate(uv.x / (2.0 * PI));
    uv.y         = saturate(uv.y / PI);
    float3 color = IBLIrradianceMap.sample(IBLMapSampler, uv, level(0)).rgb;
    return color;
}

float3 GetIBLEnvironment(texture2d<float> IBLEnvironmentMap, bool furnace, float3 dir, float lod)
{
    float2 uv    = CartesianToSphereical(normalize(dir));
    uv.x         = saturate(uv.x / (2.0 * PI));
    uv.y         = saturate(uv.y / PI);
    float3 color = IBLEnvironmentMap.sample(IBLMapSampler, uv, level(lod)).rgb;
    if (!furnace) {
        color = pow(color, 1 / 1.5);
    }
    return color;
}

float2 GetBRDFIntegrationMap(texture2d<float> IBLIntegrationLUT, float roughness, float NoV)
{
    float2 tc   = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}

float2 GetBRDFIntegrationMultiscatterMap(texture2d<float> IBLIntegrationMultiscatterLUT, float roughness, float NoV)
{
    float2 tc   = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationMultiscatterLUT.sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 fragment psmain(
             VSOutput            input                         [[stage_in]],
    constant SceneParameters&    SceneParams                   [[buffer(4)]],
    constant MaterialParameters& MaterialParams                [[buffer(5)]],
    constant DrawParameters&     DrawParams                    [[buffer(6)]],
             texture2d<float>    IBLIntegrationLUT             [[texture(0)]],
             texture2d<float>    IBLIntegrationMultiscatterLUT [[texture(1)]],
             texture2d<float>    IBLIrradianceMap              [[texture(2)]],
             texture2d<float>    IBLEnvironmentMap             [[texture(3)]])
{
    // Scene and geometry variables - world space
    float3 P   = input.PositionWS;                         // Position
    float3 N   = input.Normal;                             // Normal
    float3 T   = input.Tangent;                            // Tangent
    float3 B   = input.Bitangent;                          // Bitangent
    float3 V   = normalize((SceneParams.EyePosition - P)); // View direction
    float3 R   = reflect(-V, N);
    float  NoV = saturate(dot(N, V));

    // Material variables
    float3 baseColor          = MaterialParams.BaseColor;
    float  roughness          = MaterialParams.Roughness;
    float  metallic           = MaterialParams.Metallic;
    float  specular           = MaterialParams.Reflectance;
    float  clearCoat          = MaterialParams.ClearCoat;
    float  clearCoatRoughness = MaterialParams.ClearCoatRoughness;
    float  anisotropy         = MaterialParams.Anisotropy;
    float  dielectric         = 1 - metallic;

    // Calculate F0
    float3 F0 = (0.16 * specular * specular * dielectric) + (baseColor * metallic);

    // Remap
    float3 diffuseColor   = dielectric * baseColor;
    float  alpha          = roughness * roughness;
    float  clearCoatAlpha = clearCoatRoughness * clearCoatRoughness;

    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        // Light variables - world space
        Light  light    = SceneParams.Lights[i];
        float3 L        = normalize(light.Position - P);
        float3 H        = normalize(L + V);
        float3 Lc       = light.Color;
        float  Ls       = light.Intensity;
        float3 radiance = Lc * Ls;
        float  NoL      = saturate(dot(N, L));

        float3 Rd = diffuseColor * Fd_Lambert();

        float  cosTheta = saturate(dot(H, V));
        float  D        = 0;
        float3 F        = Fresnel_SchlickRoughness(cosTheta, F0, alpha);
        float  G        = 0;
        float  Vis      = 0;
        if (anisotropy == 0.0) {
            D   = Distribution_GGX(N, H, alpha);
            G   = Geometry_Smith(N, V, L, alpha);
            Vis = G / max(0.0001, (4.0 * NoV * NoL));
        }
        else {
            float at = max(alpha * (1.0 + anisotropy), 0.001);
            float ab = max(alpha * (1.0 - anisotropy), 0.001);

            D = DistributionAnisotropic_GGX(
                saturate(dot(N, H)), // NoH
                H,                   // H
                T,                   // T
                B,                   // B
                at,                  // at
                ab);                 // ab

            Vis = GeometryAnisotropic_SmithsGGX(
                at,                  // at
                ab,                  // ab
                saturate(dot(T, V)), // dot_c(tangent, viewDir),
                saturate(dot(B, V)), // dot_c(bitangent, viewDir),
                saturate(dot(T, L)), // dot_c(tangent, lightDir),
                saturate(dot(B, L)), // dot_c(bitangent, lightDir),
                NoV,                 // dot_c(n, viewDir),
                NoL);                // dot_c(n, lightDir));
        }

        // Specular reflectance
        float3 Rs = (D * F * Vis);

        if (SceneParams.Multiscatter) {
            float2 envBRDF            = GetBRDFIntegrationMultiscatterMap(IBLIntegrationMultiscatterLUT, saturate(dot(N, H)), alpha);
            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            Rs *= energyCompensation;
        }

        // Combine diffuse and specular
        float3 Kd   = (1 - F) * dielectric;
        float3 BRDF = Kd * Rd + Rs;

        // Clear coat
        if (clearCoat > 0) {
            D                   = Distribution_GGX(N, H, clearCoatAlpha);
            F                   = Fresnel_SchlickRoughness(cosTheta, 0.04, clearCoatAlpha);
            G                   = Geometry_Smith(N, V, L, clearCoatAlpha);
            Vis                 = G;
            float3 Rs_clearCoat = (D * F * Vis);
            BRDF                = (BRDF * (1.0 - (clearCoat * F))) + (clearCoat * Rs_clearCoat);
        }

        directLighting += BRDF * radiance * NoL;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float3 F  = Fresnel_SchlickRoughness(NoV, F0, alpha);
        float3 Kd = (1 - F) * dielectric;

        float3 Rr = R;
        if (anisotropy != 0.0) {
            Rr = GetReflectedVector(V, N, T, B, alpha, anisotropy);
        }

        // Diffuse IBL component
        float3 irradiance = GetIBLIrradiance(IBLIrradianceMap, N);
        float3 Rd         = irradiance * diffuseColor * Fd_Lambert();

        // Specular IBL component
        float  lod              = alpha * (SceneParams.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(IBLEnvironmentMap, SceneParams.Furnace, Rr, lod);
        float2 envBRDF          = (float2)0;
        float3 Rs               = (float3)0;
        if (SceneParams.Multiscatter) {
            envBRDF = GetBRDFIntegrationMultiscatterMap(IBLIntegrationMultiscatterLUT, NoV, alpha);
            Rs      = prefilteredColor * mix(envBRDF.xxx, envBRDF.yyy, F0);

            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            Rs *= energyCompensation;
        }
        else {
            envBRDF = GetBRDFIntegrationMap(IBLIntegrationLUT, NoV, alpha);
            Rs      = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        }
        float3 BRDF = Kd * Rd + Rs;

        if (clearCoat > 0) {
            float3 clearCoatFresnel = Fresnel_SchlickRoughness(NoV, 0.04, clearCoatAlpha);
            lod                     = clearCoatAlpha * (SceneParams.IBLEnvironmentNumLevels - 1);
            prefilteredColor        = GetIBLEnvironment(IBLEnvironmentMap, SceneParams.Furnace, R, lod);
            envBRDF                 = GetBRDFIntegrationMap(IBLIntegrationLUT, NoV, clearCoatAlpha);
            float3 Rs_clearCoat     = prefilteredColor * (clearCoatFresnel * envBRDF.x + envBRDF.y);
            BRDF                    = (BRDF * (1.0 - (clearCoat * clearCoatFresnel))) + (clearCoat * Rs_clearCoat);
        }

        indirectLighting = BRDF;
    }

    float3 finalColor = directLighting + indirectLighting;

    if (!SceneParams.Furnace) {
        finalColor = ACESFilm(finalColor);
    }
    return float4(finalColor, 0);
}
