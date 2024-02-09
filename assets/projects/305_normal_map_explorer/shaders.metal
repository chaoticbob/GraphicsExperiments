#include <metal_stdlib>
using namespace metal;

struct CameraProperties
{
    float4x4 ModelMatrix;
    float4x4 ViewProjectionMatrix;
    float3   EyePosition;
};

struct VSOutput
{
    float4 PositionWS;
    float4 PositionCS [[position]];
    float2 TexCoord;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
};

struct VertexData
{
    float3 PositionOS [[attribute(0)]];
    float2 TexCoord [[attribute(1)]];
    float3 Normal [[attribute(2)]];
    float3 Tangent [[attribute(3)]];
    float3 Bitangent [[attribute(4)]];
};

VSOutput vertex vsmain(
    VertexData                 vertexData [[stage_in]],
    constant CameraProperties& Camera [[buffer(5)]])
{
    // World space
    float4 Pw = (Camera.ModelMatrix * float4(vertexData.PositionOS, 1));

    // Position in clip space and texture coords
    VSOutput output;
    output.PositionWS = Pw;
    output.PositionCS = (Camera.ViewProjectionMatrix * Pw);
    output.TexCoord   = vertexData.TexCoord;
    output.Normal     = vertexData.Normal;
    output.Tangent    = vertexData.Tangent;
    output.Bitangent  = vertexData.Bitangent;

    return output;
}

constexpr sampler Sampler0{
    filter::linear,
    mip_filter::linear,
    address::repeat};

float4 fragment psmain(
    VSOutput                   input [[stage_in]],
    constant CameraProperties& Camera [[buffer(5)]],
    texture2d<float>           DiffuseTexture [[texture(0)]],
    texture2d<float>           NormalTexture [[texture(1)]])
{
    // These are in world space
    const float3 Lp = float3(4, 5, 0);
    const float3 V  = normalize(Camera.EyePosition - input.PositionWS.xyz);

    //
    // Build TBN, tangentToWorldSpace, and worldToTangentSpace matrices
    //
    // This can be done in the vertex shader as well...but they're here
    // because of laziness.
    //
    float3 vT = normalize(input.Tangent);
    float3 vB = normalize(input.Bitangent);
    float3 vN = normalize(input.Normal);
    vT        = (Camera.ModelMatrix * float4(vT, 0)).xyz;
    vB        = (Camera.ModelMatrix * float4(vB, 0)).xyz;
    vN        = (Camera.ModelMatrix * float4(vN, 0)).xyz;
    // In Metal, matrices are constructed differently
    float3x3 tangentToWorldSpace = float3x3(
        float3(vT.x, vT.y, vT.z),  // First column
        float3(vB.x, vB.y, vB.z),  // Second column
        float3(vN.x, vN.y, vN.z)); // Third column
    float3x3 worldToTangentSpace = transpose(tangentToWorldSpace);

    // These are in tangent space...except uv
    float3 E = (worldToTangentSpace * -V);
    float3 L = (worldToTangentSpace * normalize(Lp - input.PositionWS.xyz));

    // Sample textures for base color and normal
    float3 baseColor = DiffuseTexture.sample(Sampler0, input.TexCoord).rgb;
    float3 normal    = NormalTexture.sample(Sampler0, input.TexCoord).xyz;
    normal           = normal * 2.0 - 1.0;
    normal           = normalize(normal);

    // Lighting
    float  Ra         = 0.3;
    float  Rd         = saturate(dot(L, normal));
    float3 R          = reflect(-L, normal);
    float  RdotV      = saturate(dot(R, -E));
    float  Rs         = pow(RdotV, 10.0);
    float3 finalColor = (Ra + Rd) * baseColor + Rs;

    return float4(finalColor, 1);
}
