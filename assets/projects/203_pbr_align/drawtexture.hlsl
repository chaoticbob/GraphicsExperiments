
#if defined(__spirv__)
#define DEFINE_AS_PUSH_CONSTANT   [[vk::push_constant]]
#else
#define DEFINE_AS_PUSH_CONSTANT
#endif 

struct SceneParameters {
    float4x4 MVP;
};

DEFINE_AS_PUSH_CONSTANT
ConstantBuffer<SceneParameters> SceneParams       : register(b0);
SamplerState                    IBLMapSampler     : register(s1);
Texture2D                       IBLEnvironmentMap : register(t2);

struct VSOutput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD;
};

[shader("vertex")]
VSOutput vsmain(float3 PositionOS : POSITION, float2 TexCoord : TEXCOORD)
{
    VSOutput output = (VSOutput)0;
    output.PositionCS = mul(SceneParams.MVP, float4(PositionOS, 1));
    output.TexCoord = TexCoord;
    return output;
}


//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

[shader("pixel")]
float4 psmain(VSOutput input) : SV_Target
{
    float3 color = IBLEnvironmentMap.SampleLevel(IBLMapSampler, input.TexCoord, 0).xyz;
    color = ACESFilm(color);
    color = pow(color, 1 / 1.6);
    return float4(color, 1);
}
