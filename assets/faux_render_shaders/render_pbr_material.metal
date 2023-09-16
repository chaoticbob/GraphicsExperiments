#include <metal_stdlib>
using namespace metal;

#define PI      3.1415292
#define EPSILON 0.00001

#define MAX_INSTANCES         100
#define MAX_MATERIALS         100
#define MAX_MATERIAL_SAMPLERS 32
#define MAX_MATERIAL_IMAGES   1024
#define MAX_IBL_TEXTURES      1

#define SCENE_REGISTER                     4
#define CAMERA_REGISTER                    5
#define DRAW_REGISTER                      6
#define INSTANCE_BUFFER_REGISTER           7
#define MATERIAL_BUFFER_REGISTER           8
#define MATERIAL_SAMPLER_START_REGISTER    9
#define MATERIAL_IMAGES_START_REGISTER     10
#define IBL_ENV_MAP_TEXTURE_START_REGISTER 11
#define IBL_IRR_MAP_TEXTURE_START_REGISTER 12
#define IBL_INTEGRATION_LUT_REGISTER       13
#define IBL_MAP_SAMPLER_REGISTER           14
#define IBL_INTEGRATION_SAMPLER_REGISTER   15

enum MaterialFlagBits
{
    MATERIAL_FLAG_BASE_COLOR_TEXTURE         = (1 << 1),
    MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE = (1 << 2),
    MATERIAL_FLAG_NORMAL_TEXTURE             = (1 << 3),
    MATERIAL_FLAG_OCCLUSION_TEXTURE          = (1 << 4),
    MATERIAL_FLAG_EMISSIVE_TEXTURE           = (1 << 5),
};

struct SceneData
{
    uint32_t IBLEnvironmentNumLevels;
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
    uint ImageIndex;
    uint SamplerIndex;
};

struct MaterialData
{
    uint   MaterialFlags;
    float3 BaseColor;
    float  MetallicFactor;
    float  RoughnessFactor;
    uint2  BaseColorTexture;
    uint2  MetallicRoughnessTexture;
    uint2  NormalTexture;
    uint2  OcclusionTexture;
    uint2  EmissiveTexture;

    float2 TexCoordTranslate;
    float2 TexCoordScale;
    float  TexCoordRotate;
};

struct MaterialImageArray
{
    array<texture2d<float>, MAX_MATERIAL_IMAGES> Images;
};

struct MaterialSamplerArray
{
    array<sampler, MAX_MATERIAL_SAMPLERS> Samplers;
};

struct IBLIrrMapTextureArray
{
    array<texture2d<float>, MAX_IBL_TEXTURES> Textures;
};

struct IBLEnvMapTextureArray
{
    array<texture2d<float>, MAX_IBL_TEXTURES> Textures;
};

struct VSOutput
{
    float4 PositionWS;
    float4 PositionCS [[position]];
    float2 TexCoord;
    float3 Normal;
    float4 Tangent;
};

struct VertexData
{
    float3 PositionOS [[attribute(0)]];
    float2 TexCoord [[attribute(1)]];
    float3 Normal [[attribute(2)]];
    float4 Tangent [[attribute(3)]];
};

VSOutput vertex vsmain(
    VertexData             vertexData [[stage_in]],
    constant DrawData&     Draw [[buffer(DRAW_REGISTER)]],
    constant CameraData&   Camera [[buffer(CAMERA_REGISTER)]],
    constant InstanceData* Instances [[buffer(INSTANCE_BUFFER_REGISTER)]])
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
        1, 0, 0, 0, 1, 0, translate.x, translate.y, 1);

    float    cs = cos(rotate);
    float    sn = sin(rotate);
    float3x3 R  = float3x3(
        cs, sn, 0, -sn, cs, 0, 0, 0, 1);

    float3x3 S = float3x3(
        scale.x, 0, 0, 0, scale.y, 0, 0, 0, 1);

    return T * R * S;
}

float3 Fresnel_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = (float3)(1 - roughness);
    return F0 + (max(r, F0) - F0) * pow(1 - cosTheta, 5);
}

float Fd_Lambert()
{
    return 1.0 / PI;
}

// circular atan2 - converts (x,y) on a unit circle to [0, 2pi]
//
#define catan2_epsilon 0.00001
#define catan2_NAN     0.0 / 0.0 // No gaurantee this is correct

float catan2(float y, float x)
{
    float absx = abs(x);
    float absy = abs(y);
    if ((absx < catan2_epsilon) && (absy < catan2_epsilon))
    {
        return catan2_NAN;
    }
    else if ((absx > 0) && (absy == 0.0))
    {
        return 0.0;
    }
    float s = 1.5 * 3.141592;
    if (y >= 0)
    {
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
    if ((absX < 0.00001) && (absZ <= 0.00001))
    {
        // Pointing straight up
        if (pos.y > 0)
        {
            return float2(0, 0);
        }
        // Pointing straight down
        else if (pos.y < 0)
        {
            return float2(0, 3.141592);
        }
        // Something went terribly wrong
        else
        {
            return float2(catan2_NAN, catan2_NAN);
        }
    }
    float theta = catan2(pos.z, pos.x);
    float phi   = acos(pos.y);
    return float2(theta, phi);
}

float3 GetIBLIrradiance(texture2d<float> IBLIrrMapTexture, sampler IBLMapSampler, float3 dir)
{
    float2 uv    = CartesianToSpherical(normalize(dir));
    uv.x         = saturate(uv.x / (2.0 * PI));
    uv.y         = saturate(uv.y / PI);
    float3 color = IBLIrrMapTexture.sample(IBLMapSampler, uv, level(0)).rgb;
    return color;
}

float3 GetIBLEnvironment(texture2d<float> IBLEnvMapTexture, sampler IBLMapSampler, float3 dir, float lod)
{
    float2 uv    = CartesianToSpherical(normalize(dir));
    uv.x         = saturate(uv.x / (2.0 * PI));
    uv.y         = saturate(uv.y / PI);
    float3 color = IBLEnvMapTexture.sample(IBLMapSampler, uv, level(lod)).rgb;
    return color;
}

float2 GetBRDFIntegrationMap(texture2d<float> IBLIntegrationLUT, sampler IBLIntegrationSampler, float roughness, float NoV)
{
    float2 tc   = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}

/*
float2 GetBRDFIntegrationMultiscatterMap(texture2d<float> IBLIntegrationMultiscatterLUT, sampler IBLIntegrationSampler, float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationMultiscatterLUT.sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}*/

float4 fragment psmain(
    VSOutput                        input                 [[stage_in]],
	constant SceneData&             Scene                 [[buffer(SCENE_REGISTER)]],
    constant DrawData&              Draw                  [[buffer(DRAW_REGISTER)]],
    constant CameraData&            Camera                [[buffer(CAMERA_REGISTER)]],
    constant InstanceData*          Instances             [[buffer(INSTANCE_BUFFER_REGISTER)]],
    constant MaterialData*          Materials             [[buffer(MATERIAL_BUFFER_REGISTER)]],
    constant MaterialImageArray*    MaterialImages        [[buffer(MATERIAL_IMAGES_START_REGISTER)]],
    constant MaterialSamplerArray*  MaterialSamplers      [[buffer(MATERIAL_SAMPLER_START_REGISTER)]],
    constant IBLEnvMapTextureArray* IBLEnvMap             [[buffer(IBL_ENV_MAP_TEXTURE_START_REGISTER)]],
    constant IBLIrrMapTextureArray* IBLIrrMap             [[buffer(IBL_IRR_MAP_TEXTURE_START_REGISTER)]],
             texture2d<float>       IBLIntegrationLUT     [[texture(IBL_INTEGRATION_LUT_REGISTER)]],
             sampler                IBLMapSampler         [[sampler(IBL_MAP_SAMPLER_REGISTER)]],
             sampler                IBLIntegrationSampler [[sampler(IBL_INTEGRATION_SAMPLER_REGISTER)]])
{
    InstanceData instance = Instances[Draw.InstanceIndex];
    MaterialData material = Materials[Draw.MaterialIndex];

    // Transform UV to match material
    float3x3 uvTransform = CalculateTexCoordTransform(material.TexCoordTranslate, material.TexCoordRotate, material.TexCoordScale);
    float2   uv          = (uvTransform * float3(input.TexCoord, 1)).xy;

    // Base color
    float3 baseColor = material.BaseColor;
    if (material.MaterialFlags & MATERIAL_FLAG_BASE_COLOR_TEXTURE)
    {
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
    float metallic  = material.MetallicFactor;
    float roughness = material.RoughnessFactor;
    if (material.MaterialFlags & MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE)
    {
        TextureData params = {material.MetallicRoughnessTexture.x, material.MetallicRoughnessTexture.y};

        texture2d<float> tex   = MaterialImages->Images[params.ImageIndex];
        sampler          samp  = MaterialSamplers->Samplers[params.SamplerIndex];
        float3           color = tex.sample(samp, uv).rgb;
        metallic               = metallic * color.b;
        roughness              = roughness * color.g;
    }

    // Normal (N)
    float3 N = (instance.NormalMatrix * float4(input.Normal, 0)).xyz;
    if (material.MaterialFlags & MATERIAL_FLAG_NORMAL_TEXTURE)
    {
        TextureData params = {material.NormalTexture.x, material.NormalTexture.y};

        texture2d<float> tex  = MaterialImages->Images[params.ImageIndex];
        sampler          samp = MaterialSamplers->Samplers[params.SamplerIndex];
        float3           vNt  = tex.sample(samp, uv).rgb;
        vNt                   = normalize((2.0 * vNt) - 1.0);

        float3 vN = N;
        float3 vT = (instance.NormalMatrix * float4(input.Tangent.xyz, 0)).xyz;
        float3 vB = cross(vN, vT) * input.Tangent.w;
        N         = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
    }

    // Scene and geometry variables - world space
    float3 P = input.PositionWS.xyz;                // Position
    float3 V = normalize((Camera.EyePosition - P)); // View direction

    // Specular and dieletric
    float specular   = 0.5;
    float dielectric = 1.0 - metallic;

    // Remove gamma
    baseColor = pow(baseColor, 2.2);

    // Remap
    float3 diffuseColor = dielectric * baseColor;
    float  alpha        = roughness; // * roughness;

    // Calculate F0
    float3 F0 = (0.16 * specular * specular * dielectric) + (baseColor * metallic);

    // Reflection and cosine angle between N and V
    float3 R   = reflect(-V, N);
    float  NoV = saturate(dot(N, V));

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float3 F  = Fresnel_SchlickRoughness(NoV, F0, alpha);
        float3 Kd = (1 - F) * dielectric;

        float3 Rr = R;
        /*
        if (anisotropy != 0.0) {
            Rr = GetReflectedVector(V, N, vT, vB, alpha, anisotropy);
        }
        */

        // Diffuse IBL component
        float3 irradiance = GetIBLIrradiance(IBLIrrMap->Textures[0], IBLMapSampler, N);
        float3 Rd         = irradiance * diffuseColor * Fd_Lambert();

        // Specular IBL component
        float  lod              = alpha * (Scene.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(IBLEnvMap->Textures[0], IBLMapSampler, Rr, lod);
        float2 envBRDF          = (float2)0;
        float3 Rs               = (float3)0;
        /*
        if (SceneParams.Multiscatter) {
            envBRDF = GetBRDFIntegrationMultiscatterMap(IBLIintegrationMultiscatterLUT, IBLIntegratoinSampler, NoV, alpha);
            Rs = prefilteredColor * lerp(envBRDF.xxx, envBRDF.yyy, F0);

            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            Rs *= energyCompensation;
        }
        else
        */
        {
            envBRDF = GetBRDFIntegrationMap(IBLIntegrationLUT, IBLIntegrationSampler, NoV, alpha);
            Rs      = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        }
        float3 Rs_dieletric = (specular * dielectric * Rs);
        float3 Rs_metallic  = (metallic * Rs);
        float3 BRDF         = Kd * Rd + (Rs_dieletric + Rs_metallic);

        indirectLighting = BRDF;
    }

    // Final color
    float3 finalColor = indirectLighting;
    finalColor        = pow(finalColor, 1 / 2.2);

    // Output
    return float4(finalColor, 1);
}
