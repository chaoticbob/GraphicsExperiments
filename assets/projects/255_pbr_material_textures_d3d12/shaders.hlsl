#define PI 3.1415292
#define EPSILON 0.00001

#define MATERIAL_TEXTURE_STRIDE     4
#define MATERIAL_BASE_COLOR_INDEX   0
#define MATERIAL_NORMAL_INDEX       1
#define MATERIAL_ROUGHNESS_INDEX    2
#define MATERIAL_METALLIC_INDEX     3

#define NUM_MATERIALS            9
#define TOTAL_MATERIAL_TEXTURES  (NUM_MATERIALS * MATERIAL_TEXTURE_STRIDE)

struct Light
{
    uint   Active;
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
    uint     IBLIndex;
    float    IBLDiffuseStrength;
    float    IBLSpecularStrength;
    uint     Multiscatter;
    uint     ColorCorrect;
};

struct DrawParameters {
    float4x4 ModelMatrix;
    uint     MaterialIndex;
    uint     InvertNormalMapY;
};

struct MaterialParameters {
    float  Specular;
};

ConstantBuffer<SceneParameters>      SceneParams                   : register(b0);
ConstantBuffer<DrawParameters>       DrawParams                    : register(b1);
StructuredBuffer<MaterialParameters> MaterialParams                : register(t2);
Texture2D                            IBLIntegrationLUT             : register(t3);
Texture2D                            IBLIntegrationMultiscatterLUT : register(t4);
Texture2D                            IBLIrradianceMaps[32]         : register(t16);
Texture2D                            IBLEnvironmentMaps[32]        : register(t48);
SamplerState                         IBLIntegrationSampler         : register(s32);
SamplerState                         IBLMapSampler                 : register(s33);

// Material textures are in groups of 4:
//   [s40 + (MaterialIndex * MaterialTextureStride) + 0] : Albedo
//   [s40 + (MaterialIndex * MaterialTextureStride) + 1] : Normal
//   [s40 + (MaterialIndex * MaterialTextureStride) + 2] : Roughness
//   [s40 + (MaterialIndex * MaterialTextureStride) + 3] : Metallic
//
Texture2D    MaterialTextures[TOTAL_MATERIAL_TEXTURES] : register(t100);
SamplerState MaterialSampler                           : register(s34);


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
    float3 Bitangent  : BITANGENT
)
{
    VSOutput output = (VSOutput)0;
    output.PositionWS = mul(DrawParams.ModelMatrix, float4(PositionOS, 1)).xyz;
    output.PositionCS = mul(SceneParams.ViewProjectionMatrix, float4(output.PositionWS, 1));
    output.TexCoord = TexCoord;
    output.Normal = Normal; //mul(DrawParams.ModelMatrix, float4(Normal, 0)).xyz;
    output.Tangent = Tangent; //mul(DrawParams.ModelMatrix, float4(Tangent, 0)).xyz;
    output.Bitangent = Bitangent; //mul(DrawParams.ModelMatrix, float4(Bitangent, 0)).xyz;
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
    float3 color = IBLIrradianceMaps[SceneParams.IBLIndex].SampleLevel(IBLMapSampler, uv, 0).rgb;
    return color;
}

float3 GetIBLEnvironment(float3 dir, float lod)
{
    float2 uv = CartesianToSphereical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);
    float3 color = IBLEnvironmentMaps[SceneParams.IBLIndex].SampleLevel(IBLMapSampler, uv, lod).rgb;
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
    float3 V = normalize((SceneParams.EyePosition - P)); // View direction
    
    // Material indices
    uint   baseColorTexIdx     = DrawParams.MaterialIndex * MATERIAL_TEXTURE_STRIDE + MATERIAL_BASE_COLOR_INDEX;
    uint   normalTexIdx        = DrawParams.MaterialIndex * MATERIAL_TEXTURE_STRIDE + MATERIAL_NORMAL_INDEX;
    uint   roughnessTexIdx     = DrawParams.MaterialIndex * MATERIAL_TEXTURE_STRIDE + MATERIAL_ROUGHNESS_INDEX;
    uint   metallicTexIdx      = DrawParams.MaterialIndex * MATERIAL_TEXTURE_STRIDE + MATERIAL_METALLIC_INDEX;    

    // Material variables
    float  specularReflectance = MaterialParams[DrawParams.MaterialIndex].Specular;
    float2 texCoord            = input.TexCoord;
    float3 baseColor           = MaterialTextures[baseColorTexIdx].Sample(MaterialSampler, texCoord).rgb;
    float3 normal              = MaterialTextures[normalTexIdx].Sample(MaterialSampler, texCoord).rgb;
    float  roughness           = MaterialTextures[roughnessTexIdx].Sample(MaterialSampler, texCoord).r;
    float  metallic            = MaterialTextures[metallicTexIdx].Sample(MaterialSampler, texCoord).r;
    float  clearCoat           = 0;
    float  clearCoatRoughness  = 0;
    float  anisotropy          = 0;

    if (DrawParams.InvertNormalMapY) {
        normal.y = 1.0 - normal.y;
    }

    // Remove gamma
    baseColor = pow(baseColor, 2.2);

    // Remap vector to [-1, 1]
    normal = normalize(2.0 * normal - 1.0);

    // Calculate F0
    float3 F0 = 0.16 * specularReflectance * specularReflectance * (1 - metallic) + baseColor * metallic;

    // Remap
    float3 diffuseColor = (1.0 - metallic) * baseColor;
    roughness = roughness * roughness;
    clearCoatRoughness = clearCoatRoughness * clearCoatRoughness;

    // Calculate N
    float3 vNt = normal;
    float3 vN  = mul(DrawParams.ModelMatrix, float4(input.Normal, 0)).xyz;
    float3 vT  = mul(DrawParams.ModelMatrix, float4(input.Tangent, 0)).xyz;
    float3 vB  = mul(DrawParams.ModelMatrix, float4(input.Bitangent, 0)).xyz;
    float3 N   = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
    //float3 N   = normalize((vNt.y * vB) + (vNt.z * vN));
    //float3x3 TBN  = float3x3(vT.x, vT.y, vT.z,
    //                         vB.x, vB.y, vB.z,
    //                         vN.x, vN.y, vN.z);
    //float3 N = mul(vNt, TBN);

    // Reflection and cosine angle bewteen N and V 
    float3 R = reflect(-V, N);
    float  NoV = saturate(dot(N, V));    

    // Direct lighting
    float3 directLighting = (float3)0;
    for (uint i = 0; i < SceneParams.NumLights; ++i) {
        if (!SceneParams.Lights[i].Active) {
            continue;
        }

        // Light variables - world space
        Light light = SceneParams.Lights[i];
        float3 L  = normalize(light.Position - P);
        float3 H  = normalize(L + V);
        float3 Lc = light.Color;
        float  Ls = light.Intensity;
        float NoL = saturate(dot(N, L));

        float3 diffuse = diffuseColor * Fd_Lambert();
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
            float at = max(roughness * (1.0 + anisotropy), 0.001);
            float ab = max(roughness * (1.0 - anisotropy), 0.001);
            
            D = DistributionAnisotropic_GGX(
                saturate(dot(N, H)), // NoH
                H,                   // H
                vT,                   // T
                vB,                   // B
                at,                  // at
                ab);                 // ab
            
            Vis = GeometryAnisotropic_SmithsGGX(
                at,                  // at
                ab,                  // ab
                saturate(dot(vT, V)), // dot_c(tangent, viewDir), 
                saturate(dot(vB, V)), // dot_c(bitangent, viewDir),
                saturate(dot(vT, L)), // dot_c(tangent, lightDir), 
                saturate(dot(vB, L)), // dot_c(bitangent, lightDir),
                NoV,                 // dot_c(n, viewDir), 
                NoL);                // dot_c(n, lightDir));

        }
        
        // Specular reflectance
        float3 specular = (D * F * Vis); 

        if (SceneParams.Multiscatter) {
            float2 envBRDF = GetBRDFIntegrationMultiscatterMap(saturate(dot(N, H)), roughness);
            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            specular *= energyCompensation;
        }        
    
        // Combine diffuse and specular
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 BRDF = kD  * diffuse + ((specularReflectance * (1.0 - metallic)) * specular + metallic * specular);

        // Clear coat
        if (clearCoat > 0) {
            D = Distribution_GGX(N, H, clearCoatRoughness);
            F = Fresnel_SchlickRoughness(cosTheta, 0.04, clearCoatRoughness);
            G = Geometry_Smiths(N, V, L, clearCoatRoughness);
            Vis = G;
            float3 clearCoatSpecular = (D * F * Vis);

            BRDF = BRDF * (1.0 - clearCoat * F) + (clearCoat * clearCoatSpecular);
        }

        directLighting += BRDF * radiance * NoL;
    }

    // Indirect lighting
    float3 indirectLighting = (float3)0;
    {
        float  cosTheta = saturate(dot(N, V));
        float3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);

        float3 Rr = R;
        if (anisotropy != 0.0) {
            Rr = GetReflectedVector(V, N, vT, vB, roughness, anisotropy);
        }

        // Diffuse IBL component
        float3 irradiance = GetIBLIrradiance(Rr);
        float3 diffuse = irradiance * diffuseColor * Fd_Lambert();
        diffuse *= SceneParams.IBLDiffuseStrength;
        
        // Specular IBL component
        float lod = roughness * (SceneParams.IBLEnvironmentNumLevels - 1);
        float3 prefilteredColor = GetIBLEnvironment(Rr, lod);
        float2 envBRDF = (float2)0;
        float3 specular = (float3)0;
        if (SceneParams.Multiscatter) {
            envBRDF = GetBRDFIntegrationMultiscatterMap(NoV, roughness);
            specular = prefilteredColor * lerp(envBRDF.xxx, envBRDF.yyy, F0);

            float3 energyCompensation = 1.0 + F0 * (1.0 / envBRDF.y - 1.0);
            specular *= energyCompensation;
        }
        else {
            envBRDF = GetBRDFIntegrationMap(NoV, roughness);
            specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
        }
        specular *= SceneParams.IBLSpecularStrength;        

        indirectLighting = kD * diffuse + ((specularReflectance * (1.0 - metallic)) * specular + metallic * specular);

        // Clear coat
        if (clearCoat > 0) {
            float3 clearCoatFresnel = Fresnel_SchlickRoughness(cosTheta, 0.04, clearCoatRoughness);
            lod = clearCoatRoughness * (SceneParams.IBLEnvironmentNumLevels - 1);
            prefilteredColor = GetIBLEnvironment(R, lod);        
            envBRDF = GetBRDFIntegrationMap(NoV, clearCoatRoughness);
            float3 clearCoatSpecular = clearCoatSpecular = prefilteredColor * (clearCoatFresnel * envBRDF.x + envBRDF.y);

            indirectLighting = indirectLighting * (1.0 - clearCoat * clearCoatFresnel) + (clearCoat * clearCoatSpecular);
        }
    }

    float3 finalColor = directLighting + indirectLighting;

    if (SceneParams.ColorCorrect) {
        finalColor = ACESFilm(finalColor);
    }

    // Reapply gamma
    finalColor = pow(finalColor, 1 / 2.2);

    return float4(finalColor, 0);  
}