#define PI 3.1415292
#define EPSILON 0.000001

#define DISTRIBUTION_TROWBRIDGE_REITZ 0
#define DISTRIBUTION_BECKMANN         1
#define DISTRIBUTION_BLINN_PHONG      2

#define FRESNEL_SCHLICK_ROUGHNESS 0
#define FRESNEL_SCHLICK           1
#define FRESNEL_COOK_TORRANCE     2
#define FRESNEL_NONE              3

#define GEOMETRY_SMITHS        0
#define GEOMETRY_IMPLICIT      1
#define GEOMETRY_NEUMANN       2
#define GEOMETRY_COOK_TORRANCE 3
#define GEOMETRY_KELEMEN       4
#define GEOMETRY_BECKMANN      5
#define GEOMETRY_GGX1          6
#define GEOMETRY_GGX2          7
#define GEOMETRY_SCHLICK_GGX   8

struct Light
{
    float3 Position;
    float3 Color;
    float  Intensity;
};

struct SceneParameters {
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;
    uint     NumLights;
    Light    Lights[8];
    uint     IBLEnvNumLevels;
    uint     IBLIndex;
    float    IBLDiffuseStrength;
    float    IBLSpecularStrength;
};

struct DrawParameters {
    float4x4 ModelMatrix;
    uint     MaterialIndex;
};

struct MaterialParameters {
    float3 albedo;
    float  roughness;
    float  metalness;
    float3 F0;
    uint   D_Func;
    uint   F_Func;
    uint   G_Func;    
};

ConstantBuffer<SceneParameters>      SceneParams        : register(b0);
ConstantBuffer<DrawParameters>       DrawParams         : register(b1);
StructuredBuffer<MaterialParameters> MaterialParams     : register(t2);
SamplerState                         ClampedSampler     : register(s4);
SamplerState                         UWrapSampler       : register(s5);
Texture2D                            BRDFLUT            : register(t10);
Texture2D                            IrradianceMap[32]  : register(t16);
Texture2D                            EnvironmentMap[32] : register(t48);

// =================================================================================================
// Vertex Shader
// =================================================================================================
struct VSOutput {
    float3 PositionWS : POSITION;
    float4 PositionCS : SV_POSITION;
    float3 Normal     : NORMAL;
};    

VSOutput vsmain(
    float3 PositionOS : POSITION, 
    float3 Normal     : NORMAL
)
{
    VSOutput output = (VSOutput)0;
    output.PositionWS = mul(DrawParams.ModelMatrix, float4(PositionOS, 1)).xyz;
    output.PositionCS = mul(SceneParams.ViewProjectionMatrix, float4(output.PositionWS, 1));
    output.Normal = mul(DrawParams.ModelMatrix, float4(Normal, 0)).xyz;
    return output;
}

// =================================================================================================
// Pixel Shader
// =================================================================================================

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
float Distribution_Beckmann(float3 N, float3 H, float alpha)
{
    float NoH    = saturate(dot(N, H));
    float NoH2   = NoH * NoH;
    float alpha2 = max(alpha * alpha, EPSILON);
    // Lower bound for denominator must be capped to EPSILON
    // it doesn't become zero when NoH is 0.
    float den    = max((PI * alpha2 * NoH2 * NoH2), EPSILON);
    return (1 / den) * exp((NoH2 - 1.0) / (alpha2 * NoH2));
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Blinn-Phong [2]
//
float Distribution_BlinnPhong(float3 N, float3 H, float alpha)
{
    float NoH    = saturate(dot(N, H));
    float alpha2 = max(alpha * alpha, EPSILON);
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
    float3 g   = sqrt(eta*eta + c*c - 1);
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
float Geometry_Smiths(float3 N, float3 V, float3 L,  float roughness)
{    
    // Q: Where does this k come from?
    float k  = pow(roughness + 1, 2) / 8.0; 
    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));    
    float G1 = Geometry_SchlickBeckman(NoV, k);
    float G2 = Geometry_SchlickBeckman(NoL, k);
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
    return min(1, min(Ga, Gb));
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
float Geometry_Beckmann(float3 N, float3 V, float3 L, float3 H, float alpha)
{
    float NoV = saturate(dot(N, V));
    float c = NoV / (alpha * sqrt(1 - (NoV * NoV)));
    float A = (3.535 * c) + (2.2181 * c  *c);
    float B = 1 + (2.276 * c) + (2.577 * c * c);    
    return (c < 1.6) ? (A / B) : 1;
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// GGX [4]
//
// NOTE: There's an issue with this when NoV <= 0
//
float Geometry_GGX1(float3 N, float3 V, float3 H, float alpha)
{
    float NoV = dot(N, V);
    float alpha2 = max(alpha * alpha, EPSILON);
    return 2 * NoV / (NoV + sqrt(alpha2 + (1 - alpha2) * NoV * NoV));
}

//
// http://www.codinglabs.net/article_physically_based_rendering_cook_torrance.aspx
//
// NOTE: There's an issue with this when NoV <= 0
//
float Geometry_GGX2(float3 N, float3 V, float3 H, float alpha)
{
    float HoV    = saturate(dot(H, V));
    float HoV2   = HoV * HoV;
    float NoV    = saturate(dot(N, V));
    float chi    = ((HoV / NoV) > 0)? 1 : 0;
    float alpha2 = max(alpha * alpha, EPSILON);
    float tan2   = (1 - HoV2) / HoV2;
    return chi * 2 / (1 + sqrt( 1 + alpha2 * tan2));
}

//
// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// Schlick-GGX
//
float Geometry_SchlickGGX(float3 N, float3 V, float3 L, float alpha)
{
    float k  = alpha / 2.0;
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float G1 = Geometry_SchlickBeckman(NoV, k);
    float G2 = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
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
    if (func == GEOMETRY_SMITHS) {
        G = Geometry_Smiths(N, V, L, roughness);
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


float3 GetIBLIrradiance(float3 dir)
{
    float2 uv = CartesianToSphereical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);   
    float3 color = IrradianceMap[SceneParams.IBLIndex].SampleLevel(UWrapSampler, uv, 0).rgb;
    return color;
}

float3 GetIBLEnvironment(float3 dir, float lod)
{
    float2 uv = CartesianToSphereical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);   
    float3 color = EnvironmentMap[SceneParams.IBLIndex].SampleLevel(UWrapSampler, uv, lod).rgb;
    return color;
}

float2 GetBRDFIntegrationMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = BRDFLUT.Sample(ClampedSampler, tc).xy;
    return brdf;
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 psmain(VSOutput input) : SV_TARGET
{   
    // Scene and geometry variables - world space
    float3 P = input.PositionWS;                         // Position
    float3 N = input.Normal;                             // Normal
    float3 V = normalize((SceneParams.EyePosition - P)); // View direction
    float3 R = reflect(-V, N);
    float  NoV = saturate(dot(N, V));

    // Material variables
    MaterialParameters material = MaterialParams[DrawParams.MaterialIndex];
    float3 albedo = material.albedo;
    float  roughness = material.roughness;
    float  metalness = material.metalness;
    float3 F0 = material.F0;

    // Use albedo as the tint color
    F0 = lerp(F0, albedo, metalness);

    // Function selection
    uint D_Func = material.D_Func;
    uint F_Func = material.F_Func;
    uint G_Func = material.G_Func;


    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        Light light = SceneParams.Lights[i];

        // Light variables - world space
        float3 Lp = light.Position;
        float3 L  = normalize(Lp - P);
        float3 H  = normalize(L + V);
        float3 Lc = light.Color;
        float  Ls = light.Intensity;
        float NoL = saturate(dot(N, L));

        float3 diffuse = albedo / PI;
        float3 radiance = Lc * Ls;

        float  cosTheta = saturate(dot(H, V));
        float  D = Distribution(D_Func, N, H, roughness);
        float3 F = Fresnel(F_Func, cosTheta, F0, roughness);
        float  G = Geometry(G_Func, N, V, L, H, roughness);        

        // Specular reflectance
        float3 specular = (D * F * G) / max(0.0001, (4.0 * NoV * NoL));
    
        // Combine diffuse and specular
        float3 kD = (1.0 - F) * (1.0 - metalness);
        float3 BRDF = kD  * diffuse + specular;

        directLighting += BRDF * radiance * NoL;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float cosTheta = saturate(dot(N, V));

        // Diffuse IBL component
        float3 F = Fresnel(F_Func, cosTheta, F0, roughness);
        float3 kD = (1.0 - F) * (1.0 - metalness);
        float3 irradiance = GetIBLIrradiance(R);
        float3 diffuse = irradiance * albedo / PI;
        
        // Specular IBL component
        float lod = roughness * (SceneParams.IBLEnvNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(R, lod);
        float2 envBRDF = GetBRDFIntegrationMap(roughness, NoV);
        float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        // Ambient
        float3 ambient = (SceneParams.IBLDiffuseStrength * kD * diffuse) + (SceneParams.IBLSpecularStrength * specular);

        indirectLighting = ambient;
    }
    
/*
    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        // Light variables - world space
        Light light = SceneParams.Lights[i];
        float3 L  = normalize(light.Position - P);
        float3 H  = normalize(L + V);
        float3 Lc = light.Color;
        float NoL = saturate(dot(N, L));

        float3 diffuse = albedo / PI;
        float3 radiance = Lc;

        float  cosTheta = saturate(dot(H, V));
        float  D = Distribution_TrowbridgeReitz(N, H, roughness);
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
        float  G = Geometry_Smiths(N, V, L, roughness);

        // Specular reflectance
        float3 specular = (D * F * G) / max(0.0001, (4.0 * NoV * NoL));
    
        // Combine diffuse and specular
        float3 kD = (1.0 - F) * (1.0 - metalness);
        float3 BRDF = kD  * diffuse + specular;

        directLighting += BRDF * radiance * NoL;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float cosTheta = saturate(dot(N, V));

        // Diffuse IBL component
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
        float3 kD = (1.0 - F) * (1.0 - metalness);
        float3 irradiance = GetIBLIrradiance(R);
        float3 diffuse = irradiance * albedo / PI;
        
        // Specular IBL component
        float lod = roughness * (SceneParams.IBLNumEnvLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(R, lod);
        float2 envBRDF = GetBRDFIntegrationMap(roughness, NoV);
        float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        // Ambient
        float3 ambient = (SceneParams.IBLDiffuseStrength * kD * diffuse) + (SceneParams.IBLSpecularStrength * specular);

        indirectLighting = ambient;
    }
*/

    float3 finalColor = directLighting + indirectLighting;
      
    finalColor = ACESFilm(finalColor);      
    return float4(pow(finalColor, 1 / 2.2), 0);
}