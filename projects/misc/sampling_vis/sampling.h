#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;
using float2 = glm::vec2;
using float3 = glm::vec3;

inline float2 Hammersley(uint i, uint N)
{
    uint bits = (i << 16u) | (i >> 16u);
    bits      = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits      = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits      = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits      = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rdi = float(bits) * 2.3283064365386963e-10f;
    return float2(float(i) / float(N), rdi);
}

// CMJ borrowed from https://github.com/TheRealMJP/DXRPathTracer/blob/master/SampleFramework12/v1.02/Shaders/Sampling.hlsl
//
// clang-format off
inline uint CMJPermute(uint i, uint l, uint p)
{
    uint w = l - 1;
    w |= w >> 1;
    w |= w >> 2;
    w |= w >> 4;
    w |= w >> 8;
    w |= w >> 16;
    do
    {
        i ^= p; i *= 0xe170893d;
        i ^= p >> 16;
        i ^= (i & w) >> 4;
        i ^= p >> 8; i *= 0x0929eb3f;
        i ^= p >> 23;
        i ^= (i & w) >> 1; i *= 1 | p >> 27;
        i *= 0x6935fa69;
        i ^= (i & w) >> 11; i *= 0x74dcb303;
        i ^= (i & w) >> 2; i *= 0x9e501cc3;
        i ^= (i & w) >> 2; i *= 0xc860a3df;
        i &= w;
        i ^= i >> 5;
    }
    while (i >= l);
    return (i + p) % l;
}

inline float CMJRandFloat(uint i, uint p)
{
    i ^= p;
    i ^= i >> 17;
    i ^= i >> 10; i *= 0xb36534e5;
    i ^= i >> 12;
    i ^= i >> 21; i *= 0x93fc4795;
    i ^= 0xdf6e307f;
    i ^= i >> 17; i *= 1 | p >> 18;
    return i * (1.0f / 4294967808.0f);
}

// Returns a 2D sample from a particular pattern using correlated multi-jittered sampling [Kensler 2013]
inline float2 SampleCMJ2D(uint sampleIdx, uint numSamplesX, uint numSamplesY, uint pattern)
{
    uint N    = numSamplesX * numSamplesY;
    sampleIdx = CMJPermute(sampleIdx, N, pattern * 0x51633e2d);
    uint sx   = CMJPermute(sampleIdx % numSamplesX, numSamplesX, pattern * 0x68bc21eb);
    uint sy   = CMJPermute(sampleIdx / numSamplesX, numSamplesY, pattern * 0x02e5be93);
    float jx  = CMJRandFloat(sampleIdx, pattern * 0x967a889b);
    float jy  = CMJRandFloat(sampleIdx, pattern * 0x368cc8b7);
    return float2((sx + (sy + jx) / numSamplesY) / numSamplesX, (sampleIdx + jy) / N);
}
// clang-format on

std::vector<float2> GenerateSamples2DUniform(uint32_t numSamples, uint32_t seed = 0xDEADBEEF);
std::vector<float2> GenerateSamples2DHammersley(uint32_t numSamples, uint32_t seed = 0xDEADBEEF);
std::vector<float2> GenerateSamples2DCMJ(uint32_t numSamples, uint32_t seed = 0xDEADBEEF);

using GenerateSamples2DFn         = std::function<std::vector<float2>(uint32_t, uint32_t)>;
using GenerateSamplesHemisphereFn = std::function<std::vector<float3>(uint32_t, GenerateSamples2DFn, uint32_t)>;

std::vector<float3> GenerateSamplesHemisphereUniform(
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed = 0xDEADBEEF);

std::vector<float3> GenerateSamplesHemisphereCosineWeighted(
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed = 0xDEADBEEF);

std::vector<float3> GenerateSamplesHemisphereImportanceGGX(
    float               roughness,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed = 0xDEADBEEF);

std::vector<float3> GenerateSamplesHemisphereUniform(
    const float3&       direction,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed = 0xDEADBEEF);

std::vector<float3> GenerateSamplesHemisphereCosineWeighted(
    const float3&       direction,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed = 0xDEADBEEF);

std::vector<float3> GenerateSamplesHemisphereImportanceGGX(
    const float3&       direction,
    float               roughness,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed = 0xDEADBEEF);