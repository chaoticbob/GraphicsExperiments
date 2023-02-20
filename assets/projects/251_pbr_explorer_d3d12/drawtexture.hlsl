
struct SceneParameters {
    float4x4 MVP;
    uint     IBLIndex;
};

ConstantBuffer<SceneParameters> SceneParmas  : register(b0);
SamplerState                    Sampler0     : register(s1);
Texture2D                       Textures[16] : register(t32);

struct VSOutput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD;
};

 VSOutput vsmain(float3 PositionOS : POSITION, float2 TexCoord : TEXCOORD)
 {
    VSOutput output = (VSOutput)0;
    output.PositionCS = mul(SceneParmas.MVP, float4(PositionOS, 1));
    output.TexCoord = TexCoord;
    return output;
 }


//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

 float4 psmain(VSOutput input) : SV_Target
 {
    float3 color = Textures[SceneParmas.IBLIndex].SampleLevel(Sampler0, input.TexCoord, 0).xyz;
    color = ACESFilm(color);
    color = pow(color, 0.9);
    return float4(color, 1);
 }
