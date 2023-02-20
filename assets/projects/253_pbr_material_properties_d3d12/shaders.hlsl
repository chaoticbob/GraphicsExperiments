#define PI 3.1415292
#define EPSILON 0.00001

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
};

struct DrawParameters {
    float4x4 ModelMatrix;
};

struct MaterialParameters {
    float3 albedo;
    float  roughness;
    float  metalness;
    float  reflectance;
    float  clearCoat;
    float  clearCoatRoughness;
    float  anisotropy;
};

ConstantBuffer<SceneParameters>    SceneParams           : register(b0);
ConstantBuffer<DrawParameters>     DrawParams            : register(b1);
ConstantBuffer<MaterialParameters> MaterialParams        : register(b2);
Texture2D                          IBLIntegrationLUT     : register(t3);
Texture2D                          IBLIrradianceMap      : register(t4);
Texture2D                          IBLEnvironmentMap     : register(t5);
SamplerState                       IBLIntegrationSampler : register(s6);
SamplerState                       IBLMapSampler         : register(s7);

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
    output.PositionWS = mul(DrawParams.ModelMatrix, float4(PositionOS, 1)).xyz;
    output.PositionCS = mul(SceneParams.ViewProjectionMatrix, float4(output.PositionWS, 1));
    output.Normal = mul(DrawParams.ModelMatrix, float4(Normal, 0)).xyz;
    output.Tangent = mul(DrawParams.ModelMatrix, float4(Tangent, 0)).xyz;
    output.Bitangent = mul(DrawParams.ModelMatrix, float4(Bitangent, 0)).xyz;
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

float Geometry_Smiths(float3 N, float3 V, float3 L,  float roughness)
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

float V_Kelemen(float LoH) {
    return 0.25 / (LoH * LoH);
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
    return pow(color, 1 / 1.5);
}

float2 GetBRDFIntegrationMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.Sample(IBLIntegrationSampler, tc).rg;
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
    float3 albedo = MaterialParams.albedo;
    float  roughness = MaterialParams.roughness * MaterialParams.roughness;
    float  metalness = MaterialParams.metalness;
    float  reflectance = MaterialParams.reflectance;
    float  clearCoat = MaterialParams.clearCoat;
    float  clearCoatRoughness = MaterialParams.clearCoatRoughness * MaterialParams.clearCoatRoughness;
    float  anisotropy = MaterialParams.anisotropy;

    // Use albedo as the tint color
    //F0 = lerp(F0, albedo, metalness);
    float3 F0 = 0.16 * reflectance * reflectance * (1 - metalness) + albedo * metalness;
   
    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        // Light variables - world space
        Light light = SceneParams.Lights[i];
        float3 L  = normalize(light.Position - P);
        float3 H  = normalize(L + V);
        float3 Lc = light.Color;
        float  Ls = light.Intensity;
        float NoL = saturate(dot(N, L));

        float3 diffuse = albedo / PI;
        float3 radiance = Lc * Ls;

        float  cosTheta = saturate(dot(H, V));
        
        float  D = 0;
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
        float  G = 0;
        float  Vis = 0;
        if (anisotropy == 0.0) 
        {
            D = Distribution_GGX(N, H, roughness);
            G = Geometry_Smiths(N, V, L, roughness);
            Vis = G / max(0.0001, (4.0 * NoV * NoL));
        }
        else {
            float at = max(roughness * (1.0 + MaterialParams.anisotropy), 0.001);
            float ab = max(roughness * (1.0 - MaterialParams.anisotropy), 0.001);
            
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
        float3 specular = (D * F * Vis); // / max(0.0001, (4.0 * NoV * NoL));
    
        // Combine diffuse and specular
        float3 kD = (1.0 - F) * (1.0 - metalness);
        float3 BRDF = kD  * diffuse + ((reflectance * (1.0 - metalness)) * specular + metalness * specular);

        // Clear coat
        D = Distribution_GGX(N, H, clearCoatRoughness);
        F = Fresnel_SchlickRoughness(cosTheta, 0.04, clearCoatRoughness);
        G = Geometry_Smiths(N, V, L, clearCoatRoughness);
        Vis = G;
        float3 clearCoatSpecular = (D * F * Vis);

        BRDF = BRDF * (1.0 - clearCoat * F) + (clearCoat * clearCoatSpecular);

        directLighting += BRDF * radiance * NoL;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float  cosTheta = saturate(dot(N, V));
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metalness);

        float3 Rr = R;
        if (anisotropy != 0.0) {
            Rr = GetReflectedVector(V, N, T, B, roughness, anisotropy);
        }

        // Diffuse IBL component
        float3 irradiance = GetIBLIrradiance(Rr);
        float3 diffuse = irradiance * albedo / PI;
        
        // Specular IBL component
        float lod = roughness * (SceneParams.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(Rr, lod);
        float2 envBRDF = GetBRDFIntegrationMap(NoV, roughness);
        float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        indirectLighting = kD * diffuse + ((reflectance * (1.0 - metalness)) * specular + metalness * specular);

        // Clear coat
        float3 clearCoatFresnel = Fresnel_SchlickRoughness(cosTheta, 0.04, clearCoatRoughness);
        lod = clearCoatRoughness * (SceneParams.IBLEnvironmentNumLevels - 1);
        prefilteredColor = GetIBLEnvironment(R, lod);        
        envBRDF = GetBRDFIntegrationMap(NoV, clearCoatRoughness);
        float3 clearCoatSpecular = prefilteredColor * (clearCoatFresnel * envBRDF.x + envBRDF.y);

        indirectLighting = indirectLighting * (1.0 - clearCoat * clearCoatFresnel) + (clearCoat * clearCoatSpecular);
    }

    float3 finalColor = directLighting + indirectLighting;
    //float3 finalColor = indirectLighting;

    finalColor = ACESFilm(finalColor);
    return float4(finalColor, 0);  
    //finalColor = ACESFilm(finalColor);      
    //return float4(pow(finalColor, 1 / 2.2), 0);
}