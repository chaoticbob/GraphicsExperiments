#include <metal_stdlib>
using namespace metal;

#define MAX_IBLS 32

struct SceneParameters
{
    float4x4 MVP;
    uint     IBLIndex;
};

constexpr sampler Sampler0(
    mag_filter::linear,
    min_filter::linear);

struct VSOutput
{
    float4 PositionCS [[position]];
    float2 TexCoord;
};

struct VertexData
{
    float3 PositionOS [[attribute(0)]];
    float2 TexCoord [[attribute(1)]];
};

VSOutput vertex vsmain(
    VertexData                vertexData [[stage_in]],
    constant SceneParameters& SceneParams [[buffer(2)]])
{
    VSOutput output;
    output.PositionCS = SceneParams.MVP * float4(vertexData.PositionOS, 1);
    output.TexCoord   = vertexData.TexCoord;
    return output;
 }


//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 fragment psmain(
    VSOutput                    input [[stage_in]],
    constant SceneParameters&   SceneParams [[buffer(2)]],
    array<texture2d<float>, MAX_IBLS> Textures [[texture(0)]])
 {
    float3 color = Textures[SceneParams.IBLIndex].sample(Sampler0, input.TexCoord, level(0)).xyz;
    color = ACESFilm(color);
    color = pow(color, 0.9);
    return float4(color, 1);
 }
