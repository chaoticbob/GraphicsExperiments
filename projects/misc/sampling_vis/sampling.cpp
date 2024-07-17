#include "sampling.h"

#include "pcg32.h"

const float kPi = 3.14159265359f;

std::vector<float2> GenerateSamples2DUniform(uint32_t numSamples, uint32_t seed)
{
    pcg32 rng = pcg32(seed);

    std::vector<float2> samples;
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        float2 sample = float2(rng.nextFloat(), rng.nextFloat());
        samples.push_back(sample);
    }

    return samples;
}

std::vector<float2> GenerateSamples2DHammersley(uint32_t numSamples, uint32_t seed)
{
    std::vector<float2> samples;
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        float2 sample = Hammersley(i, numSamples);
        samples.push_back(sample);
    }

    return samples;
}

//
// Correlated multi-jitter - Total samples = floor(sqrt(numSamples) + 0.5))^2
//
std::vector<float2> GenerateSamples2DCMJ(uint32_t numSamples, uint32_t seed)
{
    const uint32_t numSamplesX = static_cast<uint32_t>(sqrtf(static_cast<float>(numSamples)) + 0.5f);
    const uint32_t numSamplesY = numSamplesX;

    std::vector<float2> samples;
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        float2 sample = SampleCMJ2D(i, numSamplesX, numSamplesY, seed);
        samples.push_back(sample);
    }

    return samples;
}

std::vector<float3> GenerateSamplesHemisphereUniform(
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed)
{
    auto samples2D = genSamples2DFn(numSamples, seed);

    std::vector<float3> samples;
    for (const auto& Xi : samples2D)
    {
        float m     = 1.0f;
        float phi   = 2.0f * kPi * Xi.x;
        float theta = acos(pow(1.0f - Xi.y, 1.0f / (1.0f + m)));

        float  x = sin(theta) * cos(phi);
        float  y = sin(theta) * sin(phi);
        float  z = cos(theta);
        float3 P = float3(x, y, z);

        samples.push_back(P);
    }

    return samples;
}

std::vector<float3> GenerateSamplesHemisphereCosineWeighted(
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed)
{
    auto samples2D = genSamples2DFn(numSamples, seed);

    std::vector<float3> samples;
    for (const auto& Xi : samples2D)
    {
        float phi      = 2 * kPi * Xi.x;
        float cosTheta = cos(kPi / 2 * Xi.y);
        float sinTheta = sqrt(1 - cosTheta * cosTheta);

        float  x = sinTheta * cos(phi);
        float  y = sinTheta * sin(phi);
        float  z = cosTheta;
        float3 P = float3(x, y, z);

        samples.push_back(P);
    }

    return samples;
}

std::vector<float3> GenerateSamplesHemisphereImportanceGGX(
    float               roughness,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed)
{
    const float a = roughness * roughness;

    auto samples2D = genSamples2DFn(numSamples, seed);

    std::vector<float3> samples;
    for (const auto& Xi : samples2D)
    {
        float phi      = 2 * kPi * Xi.x;
        float cosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
        float sinTheta = sqrt(1 - cosTheta * cosTheta);

        float  x = sinTheta * cos(phi);
        float  y = sinTheta * sin(phi);
        float  z = cosTheta;
        float3 P = float3(x, y, z);

        samples.push_back(P);
    }

    return samples;
}

std::vector<float3> GenerateSamplesHemisphereUniform(
    const float3&       direction,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed)
{
    auto samples = GenerateSamplesHemisphereUniform(numSamples, genSamples2DFn, seed);

    const auto& N = direction;
    for (auto& P : samples)
    {
        float3 upVector = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(0, 0, -1);
        float3 tangentX = glm::normalize(glm::cross(upVector, N));
        float3 tangentY = glm::cross(N, tangentX);

        P = tangentX * P.x + tangentY * P.y + N * P.z;
    }

    return samples;
}

std::vector<float3> GenerateSamplesHemisphereCosineWeighted(
    const float3&       direction,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed)
{
    auto samples = GenerateSamplesHemisphereCosineWeighted(numSamples, genSamples2DFn, seed);

    const auto& N = direction;
    for (auto& P : samples)
    {
        float3 upVector = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(0, 0, -1);
        float3 tangentX = glm::normalize(glm::cross(upVector, N));
        float3 tangentY = glm::cross(N, tangentX);

        P = tangentX * P.x + tangentY * P.y + N * P.z;
    }

    return samples;
}

std::vector<float3> GenerateSamplesHemisphereImportanceGGX(
    const float3&       direction,
    float               roughness,
    uint32_t            numSamples,
    GenerateSamples2DFn genSamples2DFn,
    uint32_t            seed)
{
    auto samples = GenerateSamplesHemisphereImportanceGGX(roughness, numSamples, genSamples2DFn, seed);

    const auto& N = direction;
    for (auto& P : samples)
    {
        float3 upVector = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(0, 0, -1);
        float3 tangentX = glm::normalize(glm::cross(upVector, N));
        float3 tangentY = glm::cross(N, tangentX);

        P = tangentX * P.x + tangentY * P.y + N * P.z;
    }

    return samples;
}
