#include <metal_stdlib>
using namespace metal;

#define PI 3.1415292
#define EPSILON 0.00001

#define MAX_INSTANCES         100
#define MAX_MATERIALS         100
#define MAX_MATERIAL_SAMPLERS 32
#define MAX_MATERIAL_IMAGES   1024
#define MAX_IBL_TEXTURES      1

#define CAMERA_REGISTER                     4
#define DRAW_REGISTER                       5
#define INSTANCE_BUFFER_REGISTER            6
#define MATERIAL_BUFFER_REGISTER            7
#define MATERIAL_SAMPLER_START_REGISTER     8
#define MATERIAL_IMAGES_START_REGISTER      9

enum MaterialFlagBits
{
    MATERIAL_FLAG_BASE_COLOR_TEXTURE         = (1 << 1),
    MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE = (1 << 2),
    MATERIAL_FLAG_NORMAL_TEXTURE             = (1 << 3),
    MATERIAL_FLAG_OCCLUSION_TEXTURE          = (1 << 4),
    MATERIAL_FLAG_EMISSIVE_TEXTURE           = (1 << 5),
};

struct CameraData
{
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;
};

struct DrawData
{
    uint InstanceIndex;
    uint MaterialIndex;
};

struct InstanceData
{
    float4x4 ModelMatrix;
    float4x4 NormalMatrix;
};

struct TextureData
{
    uint     ImageIndex;
    uint     SamplerIndex;
};

struct MaterialData
{
    uint    MaterialFlags;
    float3  BaseColor;
    float   MetallicFactor;
    float   RoughnessFactor;
    uint2   BaseColorTexture;
    uint2   MetallicRoughnessTexture;
    uint2   NormalTexture;
    uint2   OcclusionTexture;
    uint2   EmissiveTexture;

    float2  TexCoordTranslate;
    float2  TexCoordScale;
    float   TexCoordRotate;
};

struct MaterialImageArray
{
	array<texture2d<float>, MAX_MATERIAL_IMAGES> Images;
};

struct MaterialSamplerArray
{
	array<sampler, MAX_MATERIAL_SAMPLERS> Samplers;
};

struct VSOutput {
    float4 PositionWS;
    float4 PositionCS [[position]];
    float2 TexCoord;
    float3 Normal;
    float4 Tangent;
};

struct VertexData{
	float3 PositionOS [[attribute(0)]];
	float2 TexCoord   [[attribute(1)]];
	float3 Normal     [[attribute(2)]];
	float4 Tangent    [[attribute(3)]];
};

VSOutput vertex vsmain(
	         VertexData    vertexData [[stage_in]],
	constant DrawData&     Draw       [[buffer(DRAW_REGISTER)]],
	constant CameraData&   Camera     [[buffer(CAMERA_REGISTER)]],
	constant InstanceData* Instances  [[buffer(INSTANCE_BUFFER_REGISTER)]])
{
    InstanceData instance = Instances[Draw.InstanceIndex];

    VSOutput output;
    output.PositionWS = (instance.ModelMatrix * float4(vertexData.PositionOS, 1));
    output.PositionCS = (Camera.ViewProjectionMatrix * output.PositionWS);
    output.TexCoord   = vertexData.TexCoord;
    output.Normal     = vertexData.Normal;
    output.Tangent    = vertexData.Tangent;
    return output;
}

float3x3 CalculateTexCoordTransform(float2 translate, float rotate, float2 scale)
{
    float3x3 T = float3x3(
        1, 0, 0,
        0, 1, 0,
        translate.x, translate.y, 1);

    float cs = cos(rotate);
    float sn = sin(rotate);
    float3x3 R = float3x3(
        cs, sn, 0,
       -sn, cs, 0,
         0,  0, 1);

    float3x3 S = float3x3(
        scale.x, 0, 0,
        0, scale.y, 0,
        0, 0, 1);

    return T * R * S;
}

float4 fragment psmain(
	         VSOutput              input            [[stage_in]],
	constant DrawData&             Draw             [[buffer(DRAW_REGISTER)]],
	constant CameraData&           Camera           [[buffer(CAMERA_REGISTER)]],
	constant InstanceData*         Instances        [[buffer(INSTANCE_BUFFER_REGISTER)]],
	constant MaterialData*         Materials        [[buffer(MATERIAL_BUFFER_REGISTER)]],
	constant MaterialImageArray*   MaterialImages   [[buffer(MATERIAL_IMAGES_START_REGISTER)]],
	constant MaterialSamplerArray* MaterialSamplers [[buffer(MATERIAL_SAMPLER_START_REGISTER)]])
{
    InstanceData instance = Instances[Draw.InstanceIndex];
    MaterialData material = Materials[Draw.MaterialIndex];

    // Transform UV to match material
    float3x3 uvTransform = CalculateTexCoordTransform(material.TexCoordTranslate, material.TexCoordRotate, material.TexCoordScale);
    float2   uv          = (uvTransform * float3(input.TexCoord, 1)).xy;

    // Base color
    float3 baseColor = material.BaseColor;
    if (material.MaterialFlags & MATERIAL_FLAG_BASE_COLOR_TEXTURE) {
        TextureData params = {material.BaseColorTexture.x, material.BaseColorTexture.y};

        texture2d<float> tex  = MaterialImages->Images[params.ImageIndex];
        sampler          samp = MaterialSamplers->Samplers[params.SamplerIndex];
        baseColor             = tex.sample(samp, uv).rgb;
    }

    // Metallic and roughness
    //
    // From GLTF spec (material.pbrMetallicRoughness.metallicRoughnessTexture):
    //   The metalness values are sampled from the B channel.
    //   The roughness values are sampled from the G channel
    //
    float metallic = material.MetallicFactor;
    /*
    float roughness = material.RoughnessFactor;
    if (material.MaterialFlags & MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE) {
        TextureData params = {material.MetallicRoughnessTexture.x, material.MetallicRoughnessTexture.y};

        Texture2D tex = MaterialImages[params.ImageIndex];
        SamplerState samp = MaterialSamplers[params.SamplerIndex];
        float3 color = tex.Sample(samp, uv).rgb;
        metallic = metallic * color.b;
        roughness = roughness * color.g;
    }*/

    // Specular strength, dieletric, F0
    float specularStrength = 0.5;
    float dielectric = 1.0 - metallic;

    // Scene and geometry variables - world space
    float3 P = input.PositionWS.xyz;
    float3 V = normalize((Camera.EyePosition - P)); 
    float3 N = normalize((instance.NormalMatrix * float4(input.Normal, 0))).xyz;

    // Light
    float3 lightPos = Camera.EyePosition;
    float3 lightDir = normalize(lightPos - P);

    // Ambient contribution
    float  ambient = 0.2;

    // Diffuse reflection
    float diffuse = 0.8 * saturate(dot(N, lightDir));
    float3 Rd = (ambient + diffuse) * baseColor;

    // Specular reflection
    float3 R = reflect(-lightDir, N);
    float  RdotV = saturate(dot(R, V));
    float  Rs = pow(RdotV, 6);
    float  Rs_dieletric = (Rs * specularStrength * dielectric);
    float  Rs_metal = (Rs * metallic);

    // Final color
    float3 finalColor = (Rd + Rs_dieletric + Rs_metal);

    // Output    
    return float4(finalColor, 1);        
}
