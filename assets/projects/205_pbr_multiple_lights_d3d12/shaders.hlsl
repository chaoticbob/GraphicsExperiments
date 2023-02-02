//
// Simple PBR implementation based on:
//    https://github.com/TheCherno/Sparky/blob/master/Sandbox/shaders/AdvancedLighting.hlsl
// 
//
#define PI 3.1415292

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
};

struct DrawParameters {
    float4x4 ModelMatrix;
    uint     MaterialIndex;
};

struct MaterialParameters {
    uint UseGeometricNormal;
};

ConstantBuffer<SceneParameters>      SceneParams    : register(b0);
ConstantBuffer<DrawParameters>       DrawParams     : register(b1);
StructuredBuffer<MaterialParameters> MaterialParams : register(t2);

// Material textures are in groups of 4:
//   [t10 + (MaterialIndex * MaterialTextureStride) + 0] : Albedo
//   [t10 + (MaterialIndex * MaterialTextureStride) + 1] : Normal
//   [t10 + (MaterialIndex * MaterialTextureStride) + 2] : Roughness
//   [t10 + (MaterialIndex * MaterialTextureStride) + 3] : Metalness
//
Texture2D    MaterialTextures[8] : register(t10);
SamplerState MaterialSampler     : register(s9);

// =================================================================================================
// Vertex Shader
// =================================================================================================
struct VSOutput {
    float3 PositionWS : POSITION;
    float4 PositionCS : SV_POSITION;
    float2 TexCoord   : TEXCOORD;
    float3 Normal     : NORMAL;
    float4 Tangent    : TANGENT;
    float3 Bitangent  : BITANGENT;
};

VSOutput vsmain(
    float3 PositionOS : POSITION, 
    float2 TexCoord   : TEXCOORD,
    float3 Normal     : NORMAL,
    float4 Tangent    : TANGENT,
    float3 Bitangent  : BITANGENT)
{
    VSOutput output = (VSOutput)0;
    output.PositionWS = mul(DrawParams.ModelMatrix, float4(PositionOS, 1)).xyz;
    output.PositionCS = mul(SceneParams.ViewProjectionMatrix, float4(output.PositionWS, 1));
    output.TexCoord = TexCoord;
    output.Normal = normalize(mul(DrawParams.ModelMatrix, float4(Normal, 0)).xyz);
    output.Tangent = float4(normalize(mul(DrawParams.ModelMatrix, float4(Tangent.xyz, 0)).xyz), Tangent.w);
    output.Bitangent = normalize(mul(DrawParams.ModelMatrix, float4(Bitangent, 0)).xyz);
    return output;
}

// =================================================================================================
// Pixel Shader
// =================================================================================================
float FresnelSchlick(float F0, float Fd90, float cosTheta)
{
	return F0 + (Fd90 - F0) * pow(max(1.0 - cosTheta, 0.1), 5.0);
}

float Disney(float3 N, float3 L, float3 V, float roughness)
{
	float3 H = normalize(L + V);

	float NdotL = saturate(dot(N, L));
	float LdotH = saturate(dot(L, H));
	float NdotV = saturate(dot(N, V));

	float energyBias = lerp(0.0f, 0.5, roughness);
	float energyFactor = lerp(1.0, 1.0 / 1.51, roughness);
	float Fd90 = energyBias + 2.0 * (LdotH * LdotH) * roughness;
	float F0 = 1.0;

	float lightScatter = FresnelSchlick(F0, Fd90, NdotL);
	float viewScatter = FresnelSchlick(F0, Fd90, NdotV);

	return lightScatter * viewScatter * energyFactor;
}

float3 GGX(float3 N, float3 L, float3 V, float roughness, float specular)
{
	float3 H = normalize(L + V);
	float  NdotH = saturate(dot(N, H));

	float rough2 = max(roughness * roughness, 2.0e-3); // Capped so spec highlights don't disappear
	float rough4 = rough2 * rough2;

	float d = (NdotH * rough4 - NdotH) * NdotH + 1.0;
	float D = rough4 / (PI * (d * d));

	// Fresnel
	float3 reflectivity = specular;
	float  fresnel = 1.0;
	float  NdotL = saturate(dot(N, L));
	float  LdotH = saturate(dot(L, H));
	float  NdotV = saturate(dot(N, V));
	float3 F = reflectivity + (fresnel - fresnel * reflectivity) * exp2((-5.55473 * LdotH - 6.98316) * LdotH);

	// Geometric / Visibility
	float k = rough2 * 0.5;
	float G_SmithL = NdotL * (1.0 - k) + k;
	float G_SmithV = NdotV * (1.0 - k) + k;
	float G = 0.25 / (G_SmithL * G_SmithV);

	return G * D * F;
}

float4 psmain(VSOutput input) : SV_TARGET
{
    uint albedoIdx    = 4 * DrawParams.MaterialIndex + 0;
    uint normalIdx    = 4 * DrawParams.MaterialIndex + 1;
    uint roughnessIdx = 4 * DrawParams.MaterialIndex + 2;
    uint metalnessIdx = 4 * DrawParams.MaterialIndex + 3;

    // Read values from textures
    float3 albedo = MaterialTextures[albedoIdx].Sample(MaterialSampler, input.TexCoord).rgb;
    float3 normal = normalize((MaterialTextures[normalIdx].Sample(MaterialSampler, input.TexCoord).rgb * 2) - 1);
    float  roughness = MaterialTextures[roughnessIdx].Sample(MaterialSampler, input.TexCoord).r; 
    float  metalness = MaterialTextures[metalnessIdx].Sample(MaterialSampler, input.TexCoord).r; 
    
    // F0 in metallic workflow:
    //   - For plastics use 0.04
    //   - For metals use values closer to 1
    //
    float F0 = 0.04;
    F0 = lerp(F0, 1.0, metalness);

    // TBN for tangent space transform
    float3   nTS = input.Normal;
    float3   tTS = input.Tangent.xyz;
    float3   bTS = input.Bitangent;
    float3x3 TBN = float3x3(tTS.x, bTS.x, nTS.x,
                            tTS.y, bTS.y, nTS.y,
                            tTS.z, bTS.z, nTS.z);                                

    // Scene and geometry variables - world space
    float3 P = input.PositionWS;                         // Position
    float3 V = normalize((SceneParams.EyePosition - P)); // View direction
    float3 N = normalize(mul(TBN, normal));              // Normal
    if (MaterialParams[DrawParams.MaterialIndex].UseGeometricNormal) {
        N = input.Normal;
    }

    float3 diffuse = (float3)0;
    float3 specular = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        Light light = SceneParams.Lights[i];

        // Light variables - world space
        float3 Lp = light.Position;    // Light position in world sapce
        float3 L  = normalize(Lp - P); // Light direction vector
        float3 Lc = light.Color;       // Light color
        float  Li = light.Intensity;   // Light intensity

        float  NdotL    = saturate(dot(N, L));
        diffuse  += NdotL * Disney(N, L, V, roughness) * Lc * Li;
        specular += NdotL * GGX(N, L, V, roughness, F0) * Lc * Li;
    }

    float3 color = diffuse * albedo + specular;
    return float4(color, 0);
}