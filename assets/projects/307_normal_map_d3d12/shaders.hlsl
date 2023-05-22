struct CameraProperties {
    float4x4 ModelMatrix;
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;
};

ConstantBuffer<CameraProperties> Camera              : register(b0); // Constant buffer
Texture2D                        DiffuseTexture      : register(t1); // Texture
Texture2D                        NormalTexture       : register(t2); // Texture
SamplerState                     Sampler0            : register(s4); // Sampler

struct VSOutput {
    float4 PositionWS : POSITIONWS;
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
    // World space
    float4 Pw = mul(Camera.ModelMatrix, float4(PositionOS, 1));

    // Position in clip space and texture coords
    VSOutput output   = (VSOutput)0;
    output.PositionWS = Pw;
    output.PositionCS = mul(Camera.ViewProjectionMatrix, Pw);
    output.TexCoord   = TexCoord;
    output.Normal     = Normal;
    output.Tangent    = Tangent;
    output.Bitangent  = Bitangent;

    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{  
    // These are in world space
    const float3 Lp = float3(4, 4, 0);
    const float3 V = normalize(Camera.EyePosition - input.PositionWS.xyz);

    //
    // Build TBN, tangentToWorldSpace, and worldToTangentSpace matrices
    //
    // This can be done in the vertex shader as well...but they're here
    // because of laziness.
    //
    float3 vT = normalize(input.Tangent);
    float3 vB = normalize(input.Bitangent);
    float3 vN = normalize(input.Normal);
    vT = mul(Camera.ModelMatrix, float4(vT, 0)).xyz;
    vB = mul(Camera.ModelMatrix, float4(vB, 0)).xyz;
    vN = mul(Camera.ModelMatrix, float4(vN, 0)).xyz;
    float3x3 tangentToWorldSpace = float3x3(
        float3(vT.x, vB.x, vN.x),
        float3(vT.y, vB.y, vN.y),
        float3(vT.z, vB.z, vN.z));
    float3x3 worldToTangentSpace = transpose(tangentToWorldSpace);

    // These are in tangent space...except uv
    float3 N  = mul(worldToTangentSpace, vN);
    float3 E  = mul(worldToTangentSpace, -V);
    float3 L  = mul(worldToTangentSpace, normalize(Lp - input.PositionWS.xyz));

    // Sample textures for base color and normal
    float3 baseColor = DiffuseTexture.Sample(Sampler0, input.TexCoord).rgb;
    float3 normal = NormalTexture.Sample(Sampler0, input.TexCoord).xyz;
    normal = normal * 2.0  - 1.0;
    normal = normalize(normal);

    // Lighting    
    float  Ra = 0.3;
    float  Rd = saturate(dot(L, normal));
    float3 R = reflect(-L, normal);
    float  RdotV = saturate(dot(R, -E));
    float  Rs = pow(RdotV, 10.0);
    float3 finalColor = (Ra + Rd) * baseColor + Rs;

    return float4(finalColor, 1);
}
