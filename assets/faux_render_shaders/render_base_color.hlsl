#define PI 3.1415292
#define EPSILON 0.00001

#if defined(__spirv__)
#define DEFINE_AS_PUSH_CONSTANT   [[vk::push_constant]]
#else
#define DEFINE_AS_PUSH_CONSTANT
#endif 

#define MAX_INSTANCES         100
#define MAX_MATERIALS         100
#define MAX_MATERIAL_SAMPLERS 32
#define MAX_MATERIAL_IMAGES   1024
#define MAX_IBL_TEXTURES      1

#define CAMERA_REGISTER                     b1
#define DRAW_REGISTER                       b2
#define INSTANCE_BUFFER_REGISTER            t10
#define MATERIAL_BUFFER_REGISTER            t11
#define MATERIAL_SAMPLER_START_REGISTER     s100
#define MATERIAL_IMAGES_START_REGISTER      t200

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
    float   Metallic;
    float   Roughness;
    uint2   BaseColorTexture;
    uint2   MetallicRoughnessTexture;
    uint2   NormalTexture;
    uint2   OcclusionTexture;
    uint2   EmissiveTexture;
    
    float2  TexCoordTranslate;
    float2  TexCoordScale;
    float   TexCoordRotate;
};

ConstantBuffer<CameraData>     Camera                                  : register(CAMERA_REGISTER);                     // Camera constants

DEFINE_AS_PUSH_CONSTANT
ConstantBuffer<DrawData>       Draw                                    : register(DRAW_REGISTER);                       // Draw root constants

StructuredBuffer<InstanceData> Instances                               : register(INSTANCE_BUFFER_REGISTER);            // Instance data
StructuredBuffer<MaterialData> Materials                               : register(MATERIAL_BUFFER_REGISTER);            // Material data
SamplerState                   MaterialSamplers[MAX_MATERIAL_SAMPLERS] : register(MATERIAL_SAMPLER_START_REGISTER);     // Material samplers
Texture2D                      MaterialImages[MAX_MATERIAL_IMAGES]     : register(MATERIAL_IMAGES_START_REGISTER);      // Material images (textures)

struct VSOutput {
    float4 PositionWS : POSITIONWS;
    float4 PositionCS : SV_POSITION;
    float2 TexCoord   : TEXCOORD;
    float3 Normal     : NORMAL;
    float4 Tangent    : TANGENT;
};

VSOutput vsmain(float3 PositionOS : POSITION, float2 TexCoord : TEXCOORD, float3 Normal : NORMAL, float4 Tangent : TANGENT)
{
    InstanceData instance = Instances[Draw.InstanceIndex];
    
    VSOutput output = (VSOutput)0;
    output.PositionWS = mul(instance.ModelMatrix, float4(PositionOS, 1));
    output.PositionCS = mul(Camera.ViewProjectionMatrix, output.PositionWS);
    output.TexCoord = TexCoord;
    output.Normal = Normal;
    output.Tangent = Tangent;
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

float4 psmain(VSOutput input) : SV_TARGET
{
    InstanceData instance = Instances[Draw.InstanceIndex];
    MaterialData material = Materials[Draw.MaterialIndex];

    // Transform UV to match material
    float3x3 uvTransform = CalculateTexCoordTransform(material.TexCoordTranslate, material.TexCoordRotate, material.TexCoordScale);
    float2 uv = mul(uvTransform, float3(input.TexCoord, 1)).xy;

    // Base color
    float3 baseColor = material.BaseColor;
    if (material.MaterialFlags & MATERIAL_FLAG_BASE_COLOR_TEXTURE) {
        TextureData params = {material.BaseColorTexture.x, material.BaseColorTexture.y};

        Texture2D tex = MaterialImages[params.ImageIndex];
        SamplerState samp = MaterialSamplers[params.SamplerIndex];
        baseColor = tex.Sample(samp, uv).rgb;
    }

    // Output
    return float4(baseColor, 1); 
}