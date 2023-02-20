#define PI 3.1415292
#define EPSILON 0.000001

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
    uint     MaterialIndex;
};

struct MaterialParameters {
    float3 F0;
    uint   UseGeometricNormal;
};

ConstantBuffer<SceneParameters>      SceneParams           : register(b0);
ConstantBuffer<DrawParameters>       DrawParams            : register(b1);
StructuredBuffer<MaterialParameters> MaterialParams        : register(t2);
Texture2D                            IBLIntegrationLUT     : register(t3);
Texture2D                            IBLIrradianceMap      : register(t4);
Texture2D                            IBLEnvironmentMap     : register(t5);
SamplerState                         IBLIntegrationSampler : register(s6);
SamplerState                         IBLMapSampler         : register(s7);

// Material textures are in groups of 4:
//   [t10 + (MaterialIndex * MaterialTextureStride) + 0] : Albedo
//   [t10 + (MaterialIndex * MaterialTextureStride) + 1] : Normal
//   [t10 + (MaterialIndex * MaterialTextureStride) + 2] : Roughness
//   [t10 + (MaterialIndex * MaterialTextureStride) + 3] : Metalness
//   [t10 + (MaterialIndex * MaterialTextureStride) + 4] : Ao
//
Texture2D    MaterialTextures[10] : register(t10);
SamplerState MaterialSampler      : register(s9);

// =================================================================================================
// Vertex Shader
// =================================================================================================
struct VSOutput {
    float3 PositionWS : POSITION;
    float4 PositionCS : SV_POSITION;
    float2 TexCoord   : TEXCOORD;
    float3 Normal     : NORMAL;
    float3 Tangent    : TANGENT;
    float3 Bitangent  : BITANGENT;
};

VSOutput vsmain(
    float3 PositionOS : POSITION, 
    float2 TexCoord   : TEXCOORD,
    float3 Normal     : NORMAL,
    float3 Tangent    : TANGENT,
    float3 Bitangent  : BITANGENT)
{
    VSOutput output = (VSOutput)0;
    output.PositionWS = mul(DrawParams.ModelMatrix, float4(PositionOS, 1)).xyz;
    output.PositionCS = mul(SceneParams.ViewProjectionMatrix, float4(output.PositionWS, 1));
    output.TexCoord = TexCoord;
    output.Normal = Normal;
    output.Tangent = Tangent;
    output.Bitangent = Bitangent;
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
    return color;
}

float2 GetBRDFIntegrationMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.Sample(IBLIntegrationSampler, tc).xy;
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
    uint albedoIdx    = 5 * DrawParams.MaterialIndex + 0;
    uint normalIdx    = 5 * DrawParams.MaterialIndex + 1;
    uint roughnessIdx = 5 * DrawParams.MaterialIndex + 2;
    uint metalnessIdx = 5 * DrawParams.MaterialIndex + 3;
    uint aoIdx        = 5 * DrawParams.MaterialIndex + 4;

    // Read material values from textures
    float3 albedo    = MaterialTextures[albedoIdx].Sample(MaterialSampler, input.TexCoord).rgb;
    float3 normal    = normalize((MaterialTextures[normalIdx].Sample(MaterialSampler, input.TexCoord).rgb * 2) - 1);
    float  roughness = MaterialTextures[roughnessIdx].Sample(MaterialSampler, input.TexCoord).r; 
    float  metalness = MaterialTextures[metalnessIdx].Sample(MaterialSampler, input.TexCoord).r; 
    float3 ao        = MaterialTextures[aoIdx].Sample(MaterialSampler, input.TexCoord).rgb;
    
    // Calculate normal
    float3 vNt = normal;
    float3 vN  = mul(DrawParams.ModelMatrix, float4(input.Normal, 0)).xyz;
    float3 vT  = mul(DrawParams.ModelMatrix, float4(input.Tangent.xyz, 0)).xyz;
    float3 vB  = mul(DrawParams.ModelMatrix, float4(input.Bitangent.xyz, 0)).xyz;
    float3 N   = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);

    // Scene and geometry variables - world space
    if (MaterialParams[DrawParams.MaterialIndex].UseGeometricNormal) {
        N = input.Normal;
    }

    // Scene and geometry variables - world space
    float3 P = input.PositionWS;                         // Position
    float3 V = normalize((SceneParams.EyePosition - P)); // View direction
    float3 R = reflect(-V, N);
    float  NoV = saturate(dot(N, V));

    float3 F0 = MaterialParams[DrawParams.MaterialIndex].F0;

    // This is hack to override the F0 on the camera body
    // it's mixed materials and there isn't a map to differential
    // plastic F0 from metal F0.
    //
    if (DrawParams.MaterialIndex == 1) {
        const float3 F0_MetalAluminum  = float3(1.022, 0.782, 0.344);
        if (metalness > 0.1) {
            F0 = F0_MetalAluminum;
        }
    }

    // Use albedo as the tint color
    F0 = lerp(F0, albedo, metalness);
    
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
        float  D = Distribution_GGX(N, H, roughness);
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
        float lod = roughness * (SceneParams.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(R, lod);
        float2 envBRDF = GetBRDFIntegrationMap(roughness, NoV);
        float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        // Ambient
        float3 ambient = kD * diffuse + specular;

        indirectLighting = ambient;
    }

    float3 finalColor = (directLighting + indirectLighting) * ao;
      
    finalColor = ACESFilm(finalColor);      
    return float4(pow(finalColor, 1 / 2.2), 0);    
}