#define PI 3.1415292
#define EPSILON 0.00001

#if defined(__spirv__)
#define DEFINE_AS_PUSH_CONSTANT   [[vk::push_constant]]
#else
#define DEFINE_AS_PUSH_CONSTANT
#endif 

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
    uint     IBLEnvironmentNumLevels;
    uint     Multiscatter;
    uint     Furnace;
};

#ifdef __spirv__

// Can only use one push constant so we have to merge these two structures into one

struct MaterialParameters {
    float4x4 ModelMatrix;
    float3   BaseColor;
    float    Roughness;
    float    Metallic;
    float    Reflectance;
    float    ClearCoat;
    float    ClearCoatRoughness;
    float    Anisotropy;
};

[[vk::push_constant]]
ConstantBuffer<MaterialParameters> MaterialParams                : register(b2);

#else

struct DrawParameters {
    float4x4 ModelMatrix;
};

struct MaterialParameters {
    float3 BaseColor;
    float  Roughness;
    float  Metallic;
    float  Reflectance;
    float  ClearCoat;
    float  ClearCoatRoughness;
    float  Anisotropy;
};

ConstantBuffer<DrawParameters>     DrawParams                    : register(b1);
ConstantBuffer<MaterialParameters> MaterialParams                : register(b2);

#endif

ConstantBuffer<SceneParameters>    SceneParams                   : register(b0);
Texture2D                          IBLIntegrationLUT             : register(t3);
Texture2D                          IBLIntegrationMultiscatterLUT : register(t4);
Texture2D                          IBLIrradianceMap              : register(t5);
Texture2D                          IBLEnvironmentMap             : register(t6);
SamplerState                       IBLIntegrationSampler         : register(s32);
SamplerState                       IBLMapSampler                 : register(s33);

// =================================================================================================
// Vertex Shader
// =================================================================================================
struct VSOutput {
    float3 PositionWS : POSITION;
    float4 PositionCS : SV_POSITION;
    float3 Normal     : NORMAL;
    float3 Tangent    : TANGENT;
    float3 Bitangent  : BITANGENT;
};    

VSOutput vsmain(
    float3 PositionOS : POSITION, 
    float3 Normal     : NORMAL,
    float3 Tangent    : TANGENT,
    float3 Bitangent  : BITANGENT
)
{
    VSOutput output = (VSOutput)0;

#ifdef __spirv__
    float4x4 modelMatrix = MaterialParams.ModelMatrix;
#else
    float4x4 modelMatrix = DrawParams.ModelMatrix;
#endif
    
    output.PositionWS = mul(modelMatrix, float4(PositionOS, 1)).xyz;
    output.PositionCS = mul(SceneParams.ViewProjectionMatrix, float4(output.PositionWS, 1));
    output.Normal = mul(modelMatrix, float4(Normal, 0)).xyz;
    output.Tangent = mul(modelMatrix, float4(Tangent, 0)).xyz;
    output.Bitangent = mul(modelMatrix, float4(Bitangent, 0)).xyz;

    return output;
}

// =================================================================================================
// Pixel Shader
//
// PBR adapted from https://www.shadertoy.com/view/3tlBW7
//
// =================================================================================================

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
    float  a2 = at * ab;
    float3  v = float3(ab * ToH, at * BoH, a2 * NoH);
    float  v2 = dot(v, v);
    float  w2 = a2 / v2;
    return a2 * w2 * w2 * (1.0 / PI);
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
    float v = 0.5 / (lambdaV + lambdaL);
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

float F_Schlick90(float cosTheta, float F0, float F90) {
    return F0 + (F90 - F0) * pow(1.0 - cosTheta, 5.0);
}

float Fd_Lambert() 
{
    return 1.0 / PI;
}

float Fd_Burley(float NoV, float NoL, float LoH, float roughness) 
{
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick90(NoL, 1.0, f90);
    float viewScatter = F_Schlick90(NoV, 1.0, f90);
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
    float3 bentNormal          = normalize(lerp(N, anisotropicNormal, abs(anisotropy)));
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

float3 GetIBLIrradiance(float3 dir)
{
    float2 uv = CartesianToSphereical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);   
    float3 color = IBLIrradianceMap.SampleLevel(IBLMapSampler, uv, 0).rgb;
    return color;
}

float3 GetIBLEnvironment(float3 dir, float lod)
{
    float2 uv = CartesianToSphereical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);
    float3 color = IBLEnvironmentMap.SampleLevel(IBLMapSampler, uv, lod).rgb;
    if (!SceneParams.Furnace) {
        color = pow(color, 1 / 1.5);
    }
    return color;
}

float2 GetBRDFIntegrationMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.Sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}

float2 GetBRDFIntegrationMultiscatterMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationMultiscatterLUT.Sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 psmain(VSOutput input) : SV_TARGET
{   
    // Scene and geometry variables - world space
    float3 P = input.PositionWS;                         // Position
    float3 N = input.Normal;                             // Normal
    float3 T = input.Tangent;                            // Tangent
    float3 B = input.Bitangent;                          // Bitangent
    float3 V = normalize((SceneParams.EyePosition - P)); // View direction
    float3 R = reflect(-V, N);
    float  NoV = saturate(dot(N, V));    

    // Material variables
    float3 baseColor = MaterialParams.BaseColor;
    float  roughness = MaterialParams.Roughness;
    float  metallic = MaterialParams.Metallic;
    float  specular = MaterialParams.Reflectance;
    float  clearCoat = MaterialParams.ClearCoat;
    float  clearCoatRoughness = MaterialParams.ClearCoatRoughness;
    float  anisotropy = MaterialParams.Anisotropy;
    float  dielectric = 1 - metallic;

    // Calculate F0
    float3 F0 = (0.16 * specular * specular * dielectric) + (baseColor * metallic);

    // Remap
    float3 diffuseColor = dielectric * baseColor;
    float alpha = roughness * roughness;
    float clearCoatAlpha = clearCoatRoughness * clearCoatRoughness;
   
    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        // Light variables - world space
        Light light = SceneParams.Lights[i];
        float3 L  = normalize(light.Position - P);
        float3 H  = normalize(L + V);
        float3 Lc = light.Color;
        float  Ls = light.Intensity;
        float3 radiance = Lc * Ls;
        float  NoL = saturate(dot(N, L));

        float3 Rd = diffuseColor * Fd_Lambert();

        float  cosTheta = saturate(dot(H, V));        
        float  D = 0;
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, alpha);
        float  G = 0;
        float  Vis = 0;
        if (anisotropy == 0.0) 
        {
            D = Distribution_GGX(N, H, alpha);
            G = Geometry_Smith(N, V, L, alpha);
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
            float2 envBRDF = GetBRDFIntegrationMultiscatterMap(saturate(dot(N, H)), alpha);
            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            Rs *= energyCompensation;
        }        
    
        // Combine diffuse and specular
        float3 Kd = (1 - F) * dielectric;
        float3 BRDF = Kd * Rd + Rs;

        // Clear coat
        if (clearCoat > 0) {
            D = Distribution_GGX(N, H, clearCoatAlpha);
            F = Fresnel_SchlickRoughness(cosTheta, 0.04, clearCoatAlpha);
            G = Geometry_Smith(N, V, L, clearCoatAlpha);
            Vis = G;
            float3 Rs_clearCoat = (D * F * Vis);            
            BRDF = (BRDF * (1.0 - (clearCoat * F))) + (clearCoat * Rs_clearCoat);
        }

        directLighting += BRDF * radiance * NoL;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float3 F = Fresnel_SchlickRoughness(NoV, F0, alpha);
        float3 Kd = (1 - F) * dielectric;

        float3 Rr = R;
        if (anisotropy != 0.0) {
            Rr = GetReflectedVector(V, N, T, B, alpha, anisotropy);
        }

        // Diffuse IBL component
        float3 irradiance = GetIBLIrradiance(N);
        float3 Rd = irradiance * diffuseColor * Fd_Lambert();
        
        // Specular IBL component
        float lod = alpha * (SceneParams.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(Rr, lod);
        float2 envBRDF = (float2)0;
        float3 Rs = (float3)0;
        if (SceneParams.Multiscatter) {
            envBRDF = GetBRDFIntegrationMultiscatterMap(NoV, alpha);
            Rs = prefilteredColor * lerp(envBRDF.xxx, envBRDF.yyy, F0);

            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            Rs *= energyCompensation;
        }
        else {
            envBRDF = GetBRDFIntegrationMap(NoV, alpha);
            Rs = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        }        
        float3 BRDF = Kd * Rd + Rs;
        
        if (clearCoat > 0) {
            float3 clearCoatFresnel = Fresnel_SchlickRoughness(NoV, 0.04, clearCoatAlpha);
            lod = clearCoatAlpha * (SceneParams.IBLEnvironmentNumLevels - 1);
            prefilteredColor = GetIBLEnvironment(R, lod);        
            envBRDF = GetBRDFIntegrationMap(NoV, clearCoatAlpha);
            float3 Rs_clearCoat = prefilteredColor * (clearCoatFresnel * envBRDF.x + envBRDF.y);
            BRDF = (BRDF * (1.0 - (clearCoat * clearCoatFresnel))) + (clearCoat * Rs_clearCoat);
        }

        indirectLighting = BRDF;
    }

    float3 finalColor = directLighting + indirectLighting;    

    if (!SceneParams.Furnace) {
        finalColor = ACESFilm(finalColor);
    }
    return float4(finalColor, 0);  
}