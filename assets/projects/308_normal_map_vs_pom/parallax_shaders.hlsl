struct CameraProperties {
    float4x4 ModelMatrix;
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;
};

#ifdef __spirv__

// Root constant
struct Constants {
    //
    // Range should be [0, 1]...but anything above 0.5 is probably too much
    // Good default is 0.02
    //
    float HeightMapScale;

    bool EnableDiscard;

    bool  EnableShadow;
    float ShadowStep;

    CameraProperties Camera;
};

[[vk::push_constant]]
ConstantBuffer<Constants> PushConstants : register(b0); // All push constants

#define ROOT_CONSTANT_LOCATION PushConstants.

#else

// Root constant
cbuffer Constants : register(b5) {
    //
    // Range should be [0, 1]...but anything above 0.5 is probably too much
    // Good default is 0.02
    //
    float HeightMapScale;

    bool EnableDiscard;

    bool  EnableShadow;
    float ShadowStep;
};

ConstantBuffer<CameraProperties> Camera              : register(b0); // Constant buffer

#define ROOT_CONSTANT_LOCATION

#endif

Texture2D                        DiffuseTexture      : register(t1); // Texture
Texture2D                        NormalTexture       : register(t2); // Texture
Texture2D                        DisplacementTexture : register(t3); // Texture
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
    float4 Pw = mul(ROOT_CONSTANT_LOCATION Camera.ModelMatrix, float4(PositionOS, 1));

    // Position in clip space and texture coords
    VSOutput output   = (VSOutput)0;
    output.PositionWS = Pw;
    output.PositionCS = mul(ROOT_CONSTANT_LOCATION Camera.ViewProjectionMatrix, Pw);
    output.TexCoord   = TexCoord;
    output.Normal     = Normal;
    output.Tangent    = Tangent;
    output.Bitangent  = Bitangent;

    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{  
    // These are in world space
    const float3 Lp = float3(4, 5, 0);
    const float3 V = normalize(ROOT_CONSTANT_LOCATION Camera.EyePosition - input.PositionWS.xyz);

    //
    // Build TBN, tangentToWorldSpace, and worldToTangentSpace matrices
    //
    // This can be done in the vertex shader as well...but they're here
    // because of laziness.
    //
    float3 vT = normalize(input.Tangent);
    float3 vB = normalize(input.Bitangent);
    float3 vN = normalize(input.Normal);
    vT = mul(ROOT_CONSTANT_LOCATION Camera.ModelMatrix, float4(vT, 0)).xyz;
    vB = mul(ROOT_CONSTANT_LOCATION Camera.ModelMatrix, float4(vB, 0)).xyz;
    vN = mul(ROOT_CONSTANT_LOCATION Camera.ModelMatrix, float4(vN, 0)).xyz;
    float3x3 tangentToWorldSpace = float3x3(
        float3(vT.x, vB.x, vN.x),
        float3(vT.y, vB.y, vN.y),
        float3(vT.z, vB.z, vN.z));
    float3x3 worldToTangentSpace = transpose(tangentToWorldSpace);

    // -------------------------------------------------------------------------
    // Parallax occlusion mapping [BEGIN]
    // -------------------------------------------------------------------------
    const float  fHeightMapScale = ROOT_CONSTANT_LOCATION HeightMapScale;
    const float  nMinSamples = 32;
    const float  nMaxSamples = 64;

    // These are in tangent space...except uv
    float3 N  = mul(worldToTangentSpace, vN);
    float3 E  = mul(worldToTangentSpace, -V);
    float3 L  = mul(worldToTangentSpace, normalize(Lp - input.PositionWS.xyz));
    float2 uv = input.TexCoord;

    float  fParallaxLimit     = -length(E.xy) / E.z * fHeightMapScale;
    float2 vOffsetDir         = normalize(E.xy);
    float2 vMaxOffset         = vOffsetDir * fParallaxLimit;
    int    nNumSamples        = (int)lerp(nMaxSamples, nMinSamples, dot(E, N));
    float  fStepSize          = 1.0 / (float)nNumSamples;
    float2 dx                 = ddx(uv);
    float2 dy                 = ddy(uv);
    float  fCurrRayHeight     = 1.0;
    float2 vCurrOffset        = (float2)0;
    float2 vLastOffset        = (float2)0;
    float  fLastSampledHeight = 1.0;
    float  fCurrSampledHeight = 1.0;
    int    nCurrSample        = 0;

    while (nCurrSample < nNumSamples) {
        fCurrSampledHeight = DisplacementTexture.SampleGrad(Sampler0, uv + vCurrOffset, dx, dy).r;
        if (fCurrSampledHeight > fCurrRayHeight) {
            float delta1 = fCurrSampledHeight - fCurrRayHeight;
            float delta2 = (fCurrRayHeight + fStepSize) - fLastSampledHeight;
            float ratio  = delta1 / (delta1 + delta2);
            vCurrOffset  = (ratio * vLastOffset) + ((1.0 - ratio) * vCurrOffset);
            nCurrSample  = nNumSamples + 1;
        }
        else {
            nCurrSample++;
            fCurrRayHeight -= fStepSize;
            vLastOffset = vCurrOffset;
            vCurrOffset += fStepSize * vMaxOffset;
            fLastSampledHeight = fCurrSampledHeight;
        }
    }

    uv = uv + vCurrOffset;
    // -------------------------------------------------------------------------
    // Parallax occlusion mapping [END]
    // -------------------------------------------------------------------------

    float shadow = 0.0;
    if (ROOT_CONSTANT_LOCATION EnableShadow && (L.z > 0)) {
        fParallaxLimit = length(L.xy) / L.z * fHeightMapScale;
        vOffsetDir = normalize(L.xy);
        vMaxOffset = vOffsetDir * fParallaxLimit;
        nNumSamples = (int)lerp(nMaxSamples, nMinSamples, dot(L, N));
        fStepSize = 1.0 / (float)nNumSamples;

        vCurrOffset = (float2)0;
        vLastOffset = (float2)0;
        fCurrRayHeight = DisplacementTexture.SampleGrad(Sampler0, uv + vCurrOffset, dx, dy).r;
        fCurrSampledHeight = fCurrRayHeight;
        fLastSampledHeight = fCurrRayHeight;

        nCurrSample = 0;
        while ((nCurrSample < nNumSamples) && (fCurrRayHeight < 1)) {
            fCurrSampledHeight = DisplacementTexture.SampleGrad(Sampler0, uv + vCurrOffset, dx, dy).r;
            if (fCurrSampledHeight > fCurrRayHeight) {
                float delta1 = fCurrSampledHeight - fCurrRayHeight;
                float delta2 = (fCurrRayHeight + fStepSize) - fLastSampledHeight;
                float ratio  = delta1 / (delta1 + delta2);
                vCurrOffset  = (ratio * vLastOffset) + ((1.0 - ratio) * vCurrOffset);
                nCurrSample  = nNumSamples + 1;
                shadow = pow(ratio, 0.5);
            }
            else {
                nCurrSample++;
                fCurrRayHeight += fStepSize;
                vLastOffset = vCurrOffset;
                vCurrOffset += fStepSize * vMaxOffset;
                fLastSampledHeight = fCurrSampledHeight;
            }            
        }

        float NoL = saturate(dot(N, L));
        float factor = NoL / (25.0 * 3.141592 / 180.0);
        factor = smoothstep(0, 1, factor);
        shadow *= factor;
    }

    //
    // OPTIONAL: This creates neat looking cutouts
    //
    if (ROOT_CONSTANT_LOCATION EnableDiscard && (uv.x > 1.0 || uv.y > 1.0 || uv.x < 0.0 || uv.y < 0.0)) discard;

    // Sample textures for base color and normal
    float3 baseColor = DiffuseTexture.Sample(Sampler0, uv).rgb;
    float3 normal = NormalTexture.Sample(Sampler0, uv).xyz;
    normal = normal * 2.0  - 1.0;
    normal = normalize(normal);

    float NoL = saturate(dot(normal, L));

    // Lighting    
    float  Ra = 0.1;
    float  Rd = NoL;
    float3 R = reflect(-L, normal);
    float  RdotV = saturate(dot(R, -E));
    float  Rs = pow(RdotV, 10.0);
    float  shadowIntensity = 0.7;
    float3 finalColor = ((Ra + Rd * (1 - (shadowIntensity * shadow))) * baseColor) + (0.45 * Rs);

    return float4(finalColor, 1);
}
