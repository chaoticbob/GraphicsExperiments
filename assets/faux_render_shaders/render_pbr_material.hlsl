#define PI 3.1415292
#define EPSILON 0.00001

#define MAX_INSTANCES         100
#define MAX_MATERIALS         100
#define MAX_MATERIAL_SAMPLERS 32
#define MAX_MATERIAL_IMAGES   1024
#define MAX_IBL_TEXTURES      1

#define SCENE_REGISTER                      b0
#define CAMERA_REGISTER                     b1
#define DRAW_REGISTER                       b2
#define INSTANCE_BUFFER_REGISTER            t10
#define MATERIAL_BUFFER_REGISTER            t11
#define MATERIAL_SAMPLER_START_REGISTER     s100
#define MATERIAL_IMAGES_START_REGISTER      t200
#define IBL_ENV_MAP_TEXTURE_START_REGISTER  t32  // IBL environment texture
#define IBL_IRR_MAP_TEXTURE_START_REGISTER  t64  // IBL irradiance texture
#define IBL_INTEGRATION_LUT_REGISTER        t16  // IBL integration look up table
#define IBL_MAP_SAMPLER_REGISTER            s18  // IBL environment sampler
#define IBL_INTEGRATION_SAMPLER_REGISTER    s19  // IBL irradiance sampler

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
    uint IBLEnvironmentNumLevels;
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

ConstantBuffer<SceneData>      Scene                                   : register(SCENE_REGISTER);                     // Scene constants
ConstantBuffer<CameraData>     Camera                                  : register(CAMERA_REGISTER);                     // Camera constants
ConstantBuffer<DrawData>       Draw                                    : register(DRAW_REGISTER);                       // Draw root constants        
StructuredBuffer<InstanceData> Instances                               : register(INSTANCE_BUFFER_REGISTER);            // Instance data
StructuredBuffer<MaterialData> Materials                               : register(MATERIAL_BUFFER_REGISTER);            // Material data
SamplerState                   MaterialSamplers[MAX_MATERIAL_SAMPLERS] : register(MATERIAL_SAMPLER_START_REGISTER);     // Material samplers
Texture2D                      MaterialImages[MAX_MATERIAL_IMAGES]     : register(MATERIAL_IMAGES_START_REGISTER);      // Material images (textures)
Texture2D                      IBLEnvMapTexture[MAX_IBL_TEXTURES]      : register(IBL_ENV_MAP_TEXTURE_START_REGISTER);  // IBL environment map texture
Texture2D                      IBLIrrMapTexture[MAX_IBL_TEXTURES]      : register(IBL_IRR_MAP_TEXTURE_START_REGISTER);  // IBL irradiance map texture
Texture2D                      IBLIntegrationLUT                       : register(IBL_INTEGRATION_LUT_REGISTER);        // IBL integration LUT
SamplerState                   IBLMapSampler                           : register(IBL_MAP_SAMPLER_REGISTER);            // IBL environment/irradiance map sampler
SamplerState                   IBLIntegrationSampler                   : register(IBL_INTEGRATION_SAMPLER_REGISTER);    // IBL integration sampler

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
float2 CartesianToSpherical(float3 pos)
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
    float2 uv = CartesianToSpherical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);   
    float3 color = IBLIrrMapTexture[0].SampleLevel(IBLMapSampler, uv, 0).rgb;
    return color;
}

float3 GetIBLEnvironment(float3 dir, float lod)
{
    float2 uv = CartesianToSpherical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);
    float3 color = IBLEnvMapTexture[0].SampleLevel(IBLMapSampler, uv, lod).rgb;
    return color;
}

float2 GetBRDFIntegrationMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationLUT.Sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}

/*
float2 GetBRDFIntegrationMultiscatterMap(float roughness, float NoV)
{
    float2 tc = float2(saturate(roughness), saturate(NoV));
    float2 brdf = IBLIntegrationMultiscatterLUT.Sample(IBLIntegrationSampler, tc).rg;
    return brdf;
}*/

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

    // Metallic and roughness
    //
    // From GLTF spec (material.pbrMetallicRoughness.metallicRoughnessTexture):
    //   The metalness values are sampled from the B channel.
    //   The roughness values are sampled from the G channel
    //
    float metallic = material.MetallicFactor;
    float roughness = material.RoughnessFactor;
    if (material.MaterialFlags & MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE) {
        TextureData params = {material.MetallicRoughnessTexture.x, material.MetallicRoughnessTexture.y};

        Texture2D tex = MaterialImages[params.ImageIndex];
        SamplerState samp = MaterialSamplers[params.SamplerIndex];
        float3 color = tex.Sample(samp, uv).rgb;
        metallic = metallic * color.b;
        roughness = roughness * color.g;
    }

    // Normal (N)
    float3 N = mul(instance.NormalMatrix, float4(input.Normal, 0)).xyz;
    if (material.MaterialFlags & MATERIAL_FLAG_NORMAL_TEXTURE) {
        TextureData params = {material.NormalTexture.x, material.NormalTexture.y};
    
        Texture2D tex = MaterialImages[params.ImageIndex];
        SamplerState samp = MaterialSamplers[params.SamplerIndex];
        float3 vNt = tex.Sample(samp, uv).rgb;
        vNt = normalize((2.0 * vNt) - 1.0);

        float3 vN = N;
        float3 vT = mul(instance.NormalMatrix, float4(input.Tangent.xyz, 0)).xyz;
        float3 vB = cross(vN, vT) * input.Tangent.w;
        N = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
    }

    // Scene and geometry variables - world space
    float3 P = input.PositionWS.xyz;                    // Position
    float3 V = normalize((Camera.EyePosition - P)); // View direction    

    // Specular and dieletric
    float specular = 0.5;
    float dielectric = 1.0 - metallic;

    // Remove gamma
    baseColor = pow(baseColor, 2.2);

    // Remap
    float3 diffuseColor = dielectric * baseColor;
    float alpha = roughness; // * roughness;

    // Calculate F0
    float3 F0 = (0.16 * specular * specular * dielectric) + (baseColor * metallic);

    // Reflection and cosine angle between N and V 
    float3 R = reflect(-V, N);
    float  NoV = saturate(dot(N, V));    

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float3 F = Fresnel_SchlickRoughness(NoV, F0, alpha);
        float3 Kd = (1 - F) * dielectric;

        float3 Rr = R;
        /*
        if (anisotropy != 0.0) {
            Rr = GetReflectedVector(V, N, vT, vB, alpha, anisotropy);
        }
        */

        // Diffuse IBL component
        float3 irradiance = GetIBLIrradiance(N);
        float3 Rd = irradiance * diffuseColor * Fd_Lambert();
        
        // Specular IBL component
        float lod = alpha * (Scene.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(Rr, lod);
        float2 envBRDF = (float2)0;
        float3 Rs = (float3)0;
        /*
        if (SceneParams.Multiscatter) {
            envBRDF = GetBRDFIntegrationMultiscatterMap(NoV, alpha);
            Rs = prefilteredColor * lerp(envBRDF.xxx, envBRDF.yyy, F0);

            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            Rs *= energyCompensation;
        }
        else 
        */
        {
            envBRDF = GetBRDFIntegrationMap(NoV, alpha);
            Rs = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        }
        float3 Rs_dieletric = (specular * dielectric * Rs);
        float3 Rs_metallic = (metallic * Rs);
        float3 BRDF = Kd * Rd + (Rs_dieletric + Rs_metallic);
       
        indirectLighting = BRDF;
    }

    // Final color
    float3 finalColor = indirectLighting;
    finalColor = pow(finalColor, 1 / 2.2);

    // Output
    return float4(finalColor, 1); 
}