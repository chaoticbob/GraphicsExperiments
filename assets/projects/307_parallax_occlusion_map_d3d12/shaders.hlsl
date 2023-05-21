struct CameraProperties {
    float4x4 ModelMatrix;
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;
};

ConstantBuffer<CameraProperties> Camera              : register(b0); // Constant buffer
Texture2D                        DiffuseTexture      : register(t1); // Texture
Texture2D                        DisplacementTexture : register(t2); // Texture
Texture2D                        NormalTexture       : register(t3); // Texture
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
    // World space
    const float3 Lp = float3(4, 4, 0);

    float3 V = normalize(Camera.EyePosition - input.PositionWS.xyz);

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

    const float  fHeightMapScale = 0.01;
    const float  nMinSamples = 16;
    const float  nMaxSamples = 32;
        
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

    float2 vFinalCoords = uv + vCurrOffset;
    
    //if (vFinalCoords.x > 1.0 || vFinalCoords.y > 1.0 || vFinalCoords.x < 0.0 || vFinalCoords.y < 0.0) {
    //    discard;
    //}

    float3 vFinalNormal = NormalTexture.Sample(Sampler0, vFinalCoords).xyz;
    float3 vFinalColor  = DiffuseTexture.Sample(Sampler0, vFinalCoords).rgb;
    vFinalNormal = vFinalNormal * 2.0  - 1.0;
    vFinalNormal = normalize(vFinalNormal);
    
    //vFinalNormal = mul(tangentToWorldSpace, vFinalNormal);
    //L  = normalize(Lp - input.PositionWS.xyz);

    float Ra = 0.3;
    float Rd = saturate(dot(L, vFinalNormal));

    float3 R = reflect(-L, vFinalNormal);
    float  RdotV = saturate(dot(R, -E));
    float  Rs = pow(RdotV, 10.0);

    vFinalColor = (Ra + Rd) * vFinalColor + Rs;

    //vFinalColor = mul(tangentToWorldSpace, vFinalNormal);

    return float4(vFinalColor, 1);    
}

/*
struct VSOutput {
    float4 PositionWS : POSITIONWS;
    float4 PositionCS : SV_POSITION;
    float2 TexCoord   : TEXCOORD;
    float3 E          : EYE_DIR;
    float3 L          : LIGHT_DIR;
    float3 N          : NORMAL;
    float3 attrT      : ATTR_TANGENT;
    float3 attrB      : ATTR_BITANGENT;
    float3 attrN      : ATTR_NORMAL;
};

VSOutput vsmain(
    float3 PositionOS : POSITION, 
    float2 TexCoord   : TEXCOORD,
    float3 Normal     : NORMAL,
    float3 Tangent    : TANGENT,
    float3 Bitangent  : BITANGENT)
{
    // World space
    const float3 Lp = float3(4, 4, 0);

    // World space
    float4 Pw = mul(Camera.ModelMatrix, float4(PositionOS, 1));
    float3 P  = Pw.xyz;
    float3 E  = normalize(P - Camera.EyePosition);
    float3 L  = Lp - P;

    // Position in clip space and texture coords
    VSOutput output = (VSOutput)0;
    output.PositionWS = Pw;
    output.PositionCS = mul(Camera.ViewProjectionMatrix, Pw);
    output.TexCoord = TexCoord;

    output.attrT = Tangent;
    output.attrB = Bitangent;
    output.attrN = Normal;

    // TBN
    float3 vN = mul(Camera.ModelMatrix, float4(normalize(Normal), 0)).xyz;
    float3 vT = mul(Camera.ModelMatrix, float4(normalize(Tangent), 0)).xyz;
    float3 vB = mul(Camera.ModelMatrix, float4(normalize(Bitangent), 0)).xyz;
    float3x3 tangentToWorldSpace = float3x3(
        float3(vT.x, vB.x, vN.x),
        float3(vT.y, vB.y, vN.y),
        float3(vT.z, vB.z, vN.z));
    float3x3 worldToTangentSpace = transpose(tangentToWorldSpace);    

    // Normal from vertex attributes is in object space, so we need
    // to transform it into world space.
    //
    float3 Nw = mul(Camera.ModelMatrix, float4(Normal, 0)).xyz;

    // Transform world space vectors into tangent space
    output.E = mul(worldToTangentSpace, E);
    output.L = mul(worldToTangentSpace, L);
    output.N = mul(worldToTangentSpace, Nw);

    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{   
    const float  fHeightMapScale = 0.15;
    const float  nMinSamples = 4;
    const float  nMaxSamples = 32;

    float3 N  = normalize(input.N);
    float3 E  = normalize(input.E);
    float3 L  = normalize(input.L);
    float2 uv = input.TexCoord;
    float3 V  = -E;

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

    float2 vFinalCoords = uv + vCurrOffset;
    
    //if (vFinalCoords.x > 1.0 || vFinalCoords.y > 1.0 || vFinalCoords.x < 0.0 || vFinalCoords.y < 0.0) {
    //    discard;
    //}

    float3 vFinalNormal = NormalTexture.Sample(Sampler0, vFinalCoords).xyz;
    float3 vFinalColor  = DiffuseTexture.Sample(Sampler0, vFinalCoords).rgb;
    vFinalNormal = vFinalNormal * 2.0  - 1.0;
    vFinalNormal = normalize(vFinalNormal);

    //float3 vT = normalize(input.attrT);
    //float3 vB = normalize(input.attrB);
    //float3 vN = normalize(input.attrN);
    //float3x3 TBN = float3x3(
    //    float3(vT.x, vB.x, vN.x),
    //    float3(vT.y, vB.y, vN.y),
    //    float3(vT.z, vB.z, vN.z));
    //vFinalNormal = mul(TBN, vFinalNormal);

    float Ra = 0.3;
    float Rd = saturate(dot(L, vFinalNormal));

    float3 R = reflect(-L, vFinalNormal);
    float  RdotV = saturate(dot(R, V));
    float  Rs = pow(RdotV, 10.0);

    vFinalColor = (Ra + Rd) * vFinalColor + Rs;

    return float4(vFinalColor, 1);    
}
*/