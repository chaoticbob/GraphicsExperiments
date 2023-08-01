#include <metal_stdlib>
using namespace metal;

#define PI      3.1415292
#define EPSILON 0.000001

#define DISTRIBUTION_TROWBRIDGE_REITZ 0
#define DISTRIBUTION_BECKMANN         1
#define DISTRIBUTION_BLINN_PHONG      2

#define FRESNEL_SCHLICK_ROUGHNESS 0
#define FRESNEL_SCHLICK           1
#define FRESNEL_COOK_TORRANCE     2
#define FRESNEL_NONE              3

#define GEOMETRY_SMITH                 0
#define GEOMETRY_IMPLICIT              1
#define GEOMETRY_NEUMANN               2
#define GEOMETRY_COOK_TORRANCE         3
#define GEOMETRY_KELEMEN               4
#define GEOMETRY_BECKMANN              5
#define GEOMETRY_GGX1                  6
#define GEOMETRY_GGX2                  7
#define GEOMETRY_SCHLICK_GGX           8
#define GEOMETRY_SMITH_CORRELATED      9
#define GEOMETRY_SMITH_CORRELATED_FAST 10

#define DIRECT_COMPONENT_MODE_ALL          0
#define DIRECT_COMPONENT_MODE_DISTRIBUTION 1
#define DIRECT_COMPONENT_MODE_FRESNEL      2
#define DIRECT_COMPONENT_MODE_GEOMETRY     3
#define DIRECT_COMPONENT_MODE_DIFFUSE      4
#define DIRECT_COMPONENT_MODE_RADIANCE     5
#define DIRECT_COMPONENT_MODE_KD           6
#define DIRECT_COMPONENT_MODE_SPECULAR     7
#define DIRECT_COMPONENT_MODE_BRDF         8

#define INDIRECT_COMPONENT_MODE_ALL      0
#define INDIRECT_COMPONENT_MODE_DIFFUSE  1
#define INDIRECT_COMPONENT_MODE_SPECULAR 2

#define INDIRECT_SPECULAR_MODE_LUT        0
#define INDIRECT_SPECULAR_MODE_LAZAROV    1
#define INDIRECT_SPECULAR_MODE_POLYNOMIAL 2
#define INDIRECT_SPECULAR_MODE_KARIS      3

#define DRAW_MODE_FULL     0
#define DRAW_MODE_DIRECT   1
#define DRAW_MODE_INDIRECT 2

#if defined(__spirv__)
#    define DEFINE_AS_PUSH_CONSTANT [[vk::push_constant]]
#else
#    define DEFINE_AS_PUSH_CONSTANT
#endif

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
    uint     IBLEnvNumLevels;
    uint     IBLIndex;
    float    IBLDiffuseStrength;
    float    IBLSpecularStrength;
};

struct DrawParameters
{
    float4x4 ModelMatrix;
    uint     MaterialIndex;
};

struct MaterialParameters
{
    float3 Albedo;
    float  Roughness;
    float  Metalness;
    float  Specular;
    uint   DirectComponentMode;
    uint   D_Func;
    uint   F_Func;
    uint   G_Func;
    uint   IndirectComponentMode;
    uint   IndirectSpecularMode;
    uint   DrawMode;
};

// =================================================================================================
// Vertex Shader
// =================================================================================================
struct VSInput
{
    float3 PositionOS [[attribute(0)]];
    float3 Normal [[attribute(1)]];
};

struct VSOutput
{
    float3 PositionWS;
    float4 PositionCS [[position]];
    float3 Normal;
};

VSOutput vertex vsmain(
    VSInput                   vertexData [[stage_in]],
    constant DrawParameters&  DrawParams [[buffer(2)]],
    constant SceneParameters& SceneParams [[buffer(3)]])
{
    VSOutput output;
    output.PositionWS = (DrawParams.ModelMatrix * float4(vertexData.PositionOS, 1)).xyz;
    output.PositionCS = SceneParams.ViewProjectionMatrix * float4(output.PositionWS, 1);
    output.Normal     = (DrawParams.ModelMatrix * float4(vertexData.Normal, 0)).xyz;
    return output;
}

// =================================================================================================
// Pixel Shader
// =================================================================================================

constexpr sampler ClampedSampler(
    filter::linear,
    mip_filter::linear);

constexpr sampler UWrapSampler(
    filter::linear,
    mip_filter::linear,
    s_address::repeat);

// DEFAULT
// Trowbridge-Reitz (https://www.shadertoy.com/view/3tlBW7)
//
// NOTE: GGX is Trowbridge-Reitz
//
float Distribution_GGX(float3 N, float3 H, float roughness)
{
    float NoH    = saturate(dot(N, H));
    float NoH2   = NoH * NoH;
    float alpha2 = max(roughness * roughness, EPSILON);
    float A      = NoH2 * (alpha2 - 1) + 1;
    return alpha2 / (PI * A * A);
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Beckmann [3]
//
float Distribution_Beckmann(float3 N, float3 H, float roughness)
{
    float NoH    = saturate(dot(N, H));
    float NoH2   = NoH * NoH;
    float alpha2 = max(roughness * roughness, EPSILON);
    // Lower bound for denominator must be capped to EPSILON
    // it doesn't become zero when NoH is 0.
    float den = max((PI * alpha2 * NoH2 * NoH2), EPSILON);
    return (1 / den) * exp((NoH2 - 1.0) / (alpha2 * NoH2));
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Blinn-Phong [2]
//
float Distribution_BlinnPhong(float3 N, float3 H, float roughness)
{
    float NoH    = saturate(dot(N, H));
    float alpha2 = max(roughness * roughness, EPSILON);
    // The lower bound for the exponent argument for pow() must be
    // capped to EPSILON so that it doesn't suddenly drop to zero
    // when alpha2 is 1.
    return (1 / (PI * alpha2)) * pow(NoH, max((2.0 / alpha2 - 2.0), EPSILON));
}

// -------------------------------------------------------------------------------------------------

// DEFAULT
// Schlick with Roughness (https://www.shadertoy.com/view/3tlBW7)
//
float3 Fresnel_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = (float3)(1 - roughness);
    return F0 + (max(r, F0) - F0) * pow(1 - cosTheta, 5);
}

float3 Fresnel_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1 - F0) * pow(1 - cosTheta, 5);
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Cook-Torrance [11]
//
float3 Fresnel_CookTorrance(float cosTheta, float3 F0)
{
    float3 eta = (1 + sqrt(F0)) / (1 - sqrt(F0));
    float3 c   = (float3)cosTheta;
    float3 g   = sqrt(eta * eta + c * c - 1);
    float3 gmc = g - c;
    float3 gpc = g + c;
    float3 A   = gmc / gpc;
    float3 B   = gpc * c - 1;
    float3 C   = gmc * c + 1;
    return 1.0 / 2.0 * (A * A) * (1 + ((B / C) * (B / C)));
}

// -------------------------------------------------------------------------------------------------

// DEFAULT - supports Geometry_Smiths and Geometry_SchlickGGX
// Schlick-Beckmann (https://www.shadertoy.com/view/3tlBW7)
//
float Geometry_SchlickBeckman(float NoV, float k)
{
    return NoV / (NoV * (1 - k) + k);
}

// DEFAULT
// Smiths (https://www.shadertoy.com/view/3tlBW7)
//
float Geometry_Smith(float3 N, float3 V, float3 L, float roughness)
{
    // Q: Where does this k come from?
    float k   = pow(roughness + 1, 2) / 8.0;
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));
    float G1  = Geometry_SchlickBeckman(NoV, k);
    float G2  = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Implicit [1]
//
float Geometry_Implicit(float3 N, float3 V, float3 L, float3 H)
{
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));
    return NoL * NoV;
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Neumann  [6]
//
float Geometry_Neumann(float3 N, float3 V, float3 L, float3 H)
{
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));
    return NoL * NoV / max(NoL, NoV);
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Cook-Torrance [11]
//
float Geometry_CookTorrance(float3 N, float3 V, float3 L, float3 H)
{
    float HoN = saturate(dot(H, N));
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float VoH = saturate(dot(V, H));
    float Ga  = HoN * HoN * NoV / VoH;
    float Gb  = HoN * HoN * NoL / VoH;
    return min(1.0f, min(Ga, Gb));
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Kelemen [7]
//
float Geometry_Kelemen(float3 N, float3 V, float3 L, float3 H)
{
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));
    float VoH = saturate(dot(V, H));
    return NoL * NoV / (VoH * VoH);
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Beckmann [4]
//
// NOTE: There's an issue with this when NoV <= 0
//
float Geometry_Beckmann(float3 N, float3 V, float3 L, float3 H, float roughness)
{
    float NoV = saturate(dot(N, V));
    float c   = NoV / (roughness * sqrt(1 - (NoV * NoV)));
    float A   = (3.535 * c) + (2.2181 * c * c);
    float B   = 1 + (2.276 * c) + (2.577 * c * c);
    return (c < 1.6) ? (A / B) : 1;
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// GGX [4]
//
// NOTE: There's an issue with this when NoV <= 0
//
float Geometry_GGX1(float3 N, float3 V, float3 H, float roughness)
{
    float NoV    = dot(N, V);
    float alpha2 = max(roughness * roughness, EPSILON);
    return 2 * NoV / (NoV + sqrt(alpha2 + (1 - alpha2) * NoV * NoV));
}

//
// http://www.codinglabs.net/article_physically_based_rendering_cook_torrance.aspx
//
// NOTE: There's an issue with this when NoV <= 0
//
float Geometry_GGX2(float3 N, float3 V, float3 H, float roughness)
{
    float HoV    = saturate(dot(H, V));
    float HoV2   = HoV * HoV;
    float NoV    = saturate(dot(N, V));
    float chi    = ((HoV / NoV) > 0) ? 1 : 0;
    float alpha2 = max(roughness * roughness, EPSILON);
    float tan2   = (1 - HoV2) / HoV2;
    return chi * 2 / (1 + sqrt(1 + alpha2 * tan2));
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Schlick-GGX
//
float Geometry_SchlickGGX(float3 N, float3 V, float3 L, float roughness)
{
    float k   = roughness / 2.0;
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float G1  = Geometry_SchlickBeckman(NoV, k);
    float G2  = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
}

//
// https://google.github.io/filament/Filament.html
//
float Vis_SmithGGXCorrelated(float3 N, float3 V, float3 L, float roughness)
{
    float NoV  = saturate(dot(N, V));
    float NoL  = saturate(dot(N, L));
    float a2   = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

//
// https://google.github.io/filament/Filament.html
//
// Issue with rendering
//
float Vis_SmithGGXCorrelatedFast(float3 N, float3 V, float3 L, float roughness)
{
    float NoV  = saturate(dot(N, V));
    float NoL  = saturate(dot(N, L));
    float a    = roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

// -------------------------------------------------------------------------------------------------

float Distribution(uint func, float3 N, float3 H, float roughness)
{
    float D = 0;
    if (func == DISTRIBUTION_TROWBRIDGE_REITZ) {
        D = Distribution_GGX(N, H, roughness);
    }
    else if (func == DISTRIBUTION_BECKMANN) {
        D = Distribution_Beckmann(N, H, roughness);
    }
    else if (func == DISTRIBUTION_BLINN_PHONG) {
        D = Distribution_BlinnPhong(N, H, roughness);
    }
    return D;
}

float3 Fresnel(uint func, float cosTheta, float3 F0, float roughness)
{
    float3 F = 0;
    if (func == FRESNEL_SCHLICK_ROUGHNESS) {
        F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
    }
    else if (func == FRESNEL_SCHLICK) {
        F = Fresnel_Schlick(cosTheta, F0);
    }
    else if (func == FRESNEL_COOK_TORRANCE) {
        F = Fresnel_CookTorrance(cosTheta, F0);
    }
    else if (func == FRESNEL_NONE) {
        F = (float3)cosTheta;
    }
    return F;
}

float Geometry(uint func, float3 N, float3 V, float3 L, float3 H, float roughness)
{
    float G = 0;
    if (func == GEOMETRY_SMITH) {
        G = Geometry_Smith(N, V, L, roughness);
    }
    else if (func == GEOMETRY_IMPLICIT) {
        G = Geometry_Implicit(N, V, L, roughness);
    }
    else if (func == GEOMETRY_NEUMANN) {
        G = Geometry_Neumann(N, V, L, roughness);
    }
    else if (func == GEOMETRY_COOK_TORRANCE) {
        G = Geometry_CookTorrance(N, V, L, roughness);
    }
    else if (func == GEOMETRY_KELEMEN) {
        G = Geometry_Kelemen(N, V, L, roughness);
    }
    else if (func == GEOMETRY_BECKMANN) {
        G = Geometry_Beckmann(N, V, L, H, roughness);
    }
    else if (func == GEOMETRY_GGX1) {
        G = Geometry_GGX2(N, V, H, roughness);
    }
    else if (func == GEOMETRY_GGX2) {
        G = Geometry_GGX2(N, V, H, roughness);
    }
    else if (func == GEOMETRY_SCHLICK_GGX) {
        G = Geometry_SchlickGGX(N, V, L, roughness);
    }
    else if (func == GEOMETRY_SMITH_CORRELATED) {
        G = Geometry_SchlickGGX(N, V, L, roughness);
    }
    else if (func == GEOMETRY_SMITH_CORRELATED_FAST) {
        G = Vis_SmithGGXCorrelatedFast(N, V, L, roughness);
    }
    return G;
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
    float3 color = IBLIrradianceMap.sample(UWrapSampler, uv, level(0)).rgb;
    return color;
}

float3 GetIBLEnvironment(texture2d<float> IBLEnvironmentMap, float3 dir, float lod)
{
    float2 uv    = CartesianToSphereical(normalize(dir));
    uv.x         = saturate(uv.x / (2.0 * PI));
    uv.y         = saturate(uv.y / PI);
    float3 color = IBLEnvironmentMap.sample(UWrapSampler, uv, level(lod)).rgb;
    return color;
}

float2 GetBRDFIntegrationMap(texture2d<float> IBLIntegrationLUT, float roughness, float NoV)
{
    float2 tc   = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.sample(ClampedSampler, tc).xy;
    return brdf;
}

float3 EnvDFGLazarov(float3 specularColor, float gloss, float NoV, float3 F)
{
    float4 p0 = float4(0.5745, 1.548, -0.02397, 1.301);
    float4 p1 = float4(0.5753, -0.2511, -0.02066, 0.4755);

    float4 t = gloss * p0 + p1;

    float bias  = saturate(t.x * min(t.y, exp2(-7.672 * NoV)) + t.z);
    float delta = saturate(t.w);
    float scale = delta - bias;

    bias *= saturate(50.0 * specularColor.y);
    // return specularColor * scale + bias;
    return specularColor * (F * scale + bias);
}

float3 EnvDFGPolynomial(float3 specularColor, float gloss, float NoV, float3 F)
{
    float x = gloss;
    float y = NoV;

    float b1   = -0.1688;
    float b2   = 1.895;
    float b3   = 0.9903;
    float b4   = -4.853;
    float b5   = 8.404;
    float b6   = -5.069;
    float bias = saturate(min(b1 * x + b2 * x * x, b3 + b4 * y + b5 * y * y + b6 * y * y * y));

    float d0    = 0.6045;
    float d1    = 1.699;
    float d2    = -0.5228;
    float d3    = -3.603;
    float d4    = 1.404;
    float d5    = 0.1939;
    float d6    = 2.661;
    float delta = saturate(d0 + d1 * x + d2 * y + d3 * x * x + d4 * x * y + d5 * y * y + d6 * x * x * x);
    float scale = delta - bias;

    bias *= saturate(50.0 * specularColor.y);
    // return specularColor * scale + bias;
    return specularColor * (F * scale + bias);
}

float3 EnvBRDFApproxKaris(float3 specularColor, float roughness, float NoV, float3 F)
{
    const float4 c0   = {-1, -0.0275, -0.572, 0.022};
    const float4 c1   = {1, 0.0425, 1.04, -0.04};
    float4       r    = roughness * c0 + c1;
    float        a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2       AB   = float2(-1.04, 1.04) * a004 + r.zw;
    // return specularColor * AB.x + AB.y;
    return specularColor * (F * AB.x + AB.y);
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 fragment psmain(
    VSOutput                     input [[stage_in]],
    constant DrawParameters&     DrawParams [[buffer(2)]],
    constant SceneParameters&    SceneParams [[buffer(3)]],
    constant MaterialParameters* MaterialParams [[buffer(4)]],
    texture2d<float>             BRDFLUT [[texture(0)]],
    array<texture2d<float>, 32>  IrradianceMap [[texture(16)]],
    array<texture2d<float>, 32>  EnvironmentMap [[texture(48)]])
{
    // Scene and geometry variables - world space
    float3 P   = input.PositionWS;                         // Position
    float3 N   = normalize(input.Normal);                  // Normal
    float3 V   = normalize((SceneParams.EyePosition - P)); // View direction
    float3 R   = reflect(-V, N);
    float  NoV = saturate(dot(N, V));

    // Material variables
    MaterialParameters material   = MaterialParams[DrawParams.MaterialIndex];
    float3             baseColor  = material.Albedo;
    float              roughness  = material.Roughness;
    float              metallic   = material.Metalness;
    float              specular   = material.Specular;
    float              dielectric = 1 - metallic;

    // Calculate F0
    float3 F0 = (0.16 * specular * specular * dielectric) + (baseColor * metallic);

    // Remap
    float3 diffuseColor = dielectric * baseColor;
    float  alpha        = roughness * roughness;

    /*
    // Use albedo as the tint color
    F0 = lerp(F0, albedo, metalness);
    */

    // Function selection
    uint D_Func = material.D_Func;
    uint F_Func = material.F_Func;
    uint G_Func = material.G_Func;

    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        Light light = SceneParams.Lights[i];

        // Light variables - world space
        float3 Lp       = light.Position;
        float3 L        = normalize(Lp - P);
        float3 H        = normalize(L + V);
        float3 Lc       = light.Color;
        float  Ls       = light.Intensity;
        float3 radiance = Lc * Ls;
        float  NoL      = saturate(dot(N, L));

        float3 Rd = baseColor / PI;

        float  cosTheta = saturate(dot(H, V));
        float  D        = Distribution(D_Func, N, H, alpha);
        float3 F        = Fresnel(F_Func, cosTheta, F0, alpha);
        float  G        = Geometry(G_Func, N, V, L, H, alpha);

        // Specular reflectance
        float3 Rs = (D * F * G) / max(0.0001, (4.0 * NoV * NoL));

        // Combine diffuse and specular
        float3 Kd   = (1.0 - F) * dielectric;
        float3 BRDF = Kd * Rd + Rs;

        // Direct contribution
        float3 direct = BRDF * radiance * NoL;
        if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_DISTRIBUTION) {
            direct = D;
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_FRESNEL) {
            direct = 1.0 - dot(H, V);
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_GEOMETRY) {
            direct = G;
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_DIFFUSE) {
            direct = Rd;
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_RADIANCE) {
            direct = radiance;
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_KD) {
            direct = Kd;
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_SPECULAR) {
            direct = Rs;
        }
        else if (material.DirectComponentMode == DIRECT_COMPONENT_MODE_BRDF) {
            direct = BRDF;
        }

        // Accumulate contribution
        directLighting += direct;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        // Diffuse IBL component
        float3 F          = Fresnel(F_Func, NoV, F0, alpha);
        float3 Kd         = (1 - F) * dielectric;
        float3 irradiance = GetIBLIrradiance(IrradianceMap[SceneParams.IBLIndex], N);
        float3 Rd         = irradiance * baseColor / PI;

        // Specular IBL component
        float  lod              = alpha * (SceneParams.IBLEnvNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(EnvironmentMap[SceneParams.IBLIndex], R, lod);
        float2 envBRDF          = GetBRDFIntegrationMap(BRDFLUT, alpha, NoV);
        float3 Rs               = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        if (material.IndirectSpecularMode == INDIRECT_SPECULAR_MODE_LAZAROV) {
            Rs = EnvDFGLazarov(prefilteredColor, 1 - alpha, NoV, F);
        }
        else if (material.IndirectSpecularMode == INDIRECT_SPECULAR_MODE_POLYNOMIAL) {
            Rs = EnvDFGPolynomial(prefilteredColor, 1 - alpha, NoV, F);
        }
        else if (material.IndirectSpecularMode == INDIRECT_SPECULAR_MODE_KARIS) {
            Rs = EnvBRDFApproxKaris(prefilteredColor, alpha, NoV, F);
        }

        // Indirect contribution
        indirectLighting = (SceneParams.IBLDiffuseStrength * Kd * Rd) + (SceneParams.IBLSpecularStrength * Rs);

        if (material.IndirectComponentMode == INDIRECT_COMPONENT_MODE_DIFFUSE) {
            indirectLighting = SceneParams.IBLDiffuseStrength * Kd * Rd;
        }
        else if (material.IndirectComponentMode == INDIRECT_COMPONENT_MODE_SPECULAR) {
            indirectLighting = SceneParams.IBLSpecularStrength * Rs;
        }
    }

    float3 finalColor = directLighting + indirectLighting;
    if (material.DrawMode == DRAW_MODE_DIRECT) {
        finalColor = directLighting;
    }
    else if (material.DrawMode == DRAW_MODE_INDIRECT) {
        finalColor = indirectLighting;
    }

    finalColor = ACESFilm(finalColor);
    return float4(pow(finalColor, 1 / 2.2), 0);
}
