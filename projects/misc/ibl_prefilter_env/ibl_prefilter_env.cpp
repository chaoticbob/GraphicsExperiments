
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
using namespace glm;

#include "bitmap.h"

#define PI 3.1415926535897932384626433832795f

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

BitmapRGBA32f      gEnvironmentMap;
std::vector<float> gGaussianKernel;

// circular atan2 - converts (x,y) on a unit circle to [0, 2pi]
//
#define catan2_epsilon 0.00001f
#define catan2_NAN     NAN

float catan2(float y, float x)
{
    float absx = abs(x);
    float absy = abs(y);
    if ((absx < catan2_epsilon) && (absy < catan2_epsilon)) {
        return catan2_NAN;
    }
    else if ((absx > 0) && (absy == 0.0)) {
        return 0.0f;
    }
    float s = 1.5f * 3.141592f;
    if (y >= 0) {
        s = 3.141592f / 2.0f;
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
float2 CartesianToSpherical(float3 pos)
{
    float absX = abs(pos.x);
    float absZ = abs(pos.z);
    // Handle pos pointing straight up or straight down
    if ((absX < 0.00001) && (absZ <= 0.00001f)) {
        // Pointing straight up
        if (pos.y > 0) {
            return float2(0, 0);
        }
        // Pointing straight down
        else if (pos.y < 0) {
            return float2(0, 3.141592f);
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

// Convert spherical coordinates to cartesian coordinates.
//
// theta is the azimuth angle between [0, 2pi].
// phi is the polar angle between [0, pi].
//
float3 SphericalToCartesian(float theta, float phi)
{
    theta = fmod(theta, 2 * PI);
    phi   = fmod(phi, PI);

    float x = sin(phi) * cos(theta);
    float y = cos(phi);
    float z = sin(phi) * sin(theta);

    return float3(x, y, z);
}

float saturate(float x)
{
    return glm::clamp(x, 0.0f, 1.0f);
}

float3 ImportanceSampleGGX(float2 Xi, float Roughness, float3 N)
{
    float a        = Roughness * Roughness;
    float Phi      = 2 * PI * Xi.x;
    float CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
    float SinTheta = sqrt(1 - CosTheta * CosTheta);

    float3 H        = float3(0);
    H.x             = SinTheta * cos(Phi);
    H.y             = SinTheta * sin(Phi);
    H.z             = CosTheta;
    float3 UpVector = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 TangentX = normalize(cross(UpVector, N));
    float3 TangentY = cross(N, TangentX);

    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

//
// Taken from https://github.com/SaschaWillems/Vulkan-glTF-PBR/blob/master/data/shaders/genbrdflut.frag
// Based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
//
float2 Hammersley(uint i, uint N)
{
    uint bits = (i << 16u) | (i >> 16u);
    bits      = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits      = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits      = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits      = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rdi = float(bits) * 2.3283064365386963e-10f;
    return float2(float(i) / float(N), rdi);
}

float3 PrefilterEnvMap(float Roughness, float3 R)
{
    float3 N                = R;
    float3 V                = R;
    float3 PrefilteredColor = float3(0);
    float  TotalWeight      = 0;

    const uint NumSamples = 1024;
    for (uint i = 0; i < NumSamples; i++) {
        float2 Xi  = Hammersley(i, NumSamples);
        float3 H   = ImportanceSampleGGX(Xi, Roughness, N);
        float3 L   = 2 * dot(V, H) * H - V;
        float  NoL = saturate(dot(N, L));
        if (NoL > 0) {
            //
            // Original HLSL code:
            //    PrefilteredColor += EnvMap.SampleLevel(EnvMapSampler, L, 0).rgb * NoL;
            //
            float2 uv  = CartesianToSpherical(glm::normalize(L));
            uv.x       = saturate(uv.x / (2.0f * PI));
            uv.y       = saturate(uv.y / PI);
            //
            // Altenative Gaussian sampler:
            // auto pixel = gEnvMap.GetGaussianSampleUV(uv.x, uv.y, gGaussianKernel, BITMAP_SAMPLE_MODE_WRAP, BITMAP_SAMPLE_MODE_BORDER);
            //
            auto pixel = gEnvironmentMap.GetBilinearSampleUV(uv.x, uv.y, BITMAP_SAMPLE_MODE_WRAP, BITMAP_SAMPLE_MODE_BORDER);
            PrefilteredColor.r += pixel.r;
            PrefilteredColor.g += pixel.g;
            PrefilteredColor.b += pixel.b;

            TotalWeight += NoL;
        }
    }
    return PrefilteredColor / TotalWeight;
}

// =============================================================================
// Main
// =============================================================================

int              gNumThreads = 128;
int              gResX       = 0;
int              gResY       = 0;
float            gDu         = 0;
float            gDv         = 0;
float            gRoughness  = 0;
std::vector<int> gScanlines;
std::mutex       gScanlineMutex;
BitmapRGBA32f*   gIrradianceSource = nullptr;
BitmapRGBA32f*   gTarget           = nullptr;
uint32_t         gTargetYOffset    = 0;
uint32_t         gNumLevels        = 0;
uint32_t         gCurrentLevel     = 0;

int GetNextScanline()
{
    std::lock_guard<std::mutex> lock(gScanlineMutex);

    int scanline = -1;
    if (!gScanlines.empty()) {
        scanline = gScanlines.back();
        gScanlines.pop_back();

        // Print every 32 scanlines
        size_t n = gResY - gScanlines.size();
        if (((n % 32) == 0) || (n == 0) || (n == gResY)) {
            float percent = n / static_cast<float>(gResY) * 100.0f;
            std::cout << "Procssing level " << gCurrentLevel << "/" << (gNumLevels - 1) << ": " << std::fixed << std::setw(4) << std::setprecision(2) << percent << "% complete" << std::endl;
        }
    }

    return scanline;
}

void ProcessScanlineEnvironmentMap()
{
    int y = GetNextScanline();
    while (y != -1) {
        float4* pPixels = reinterpret_cast<float4*>(gTarget->GetPixels(0, y + gTargetYOffset));

        for (int x = 0; x < gResX; ++x) {
            float  theta  = (x * gDu) * 2 * PI;
            float  phi    = (y * gDv) * PI * 0.99999f;
            float3 R      = glm::normalize(SphericalToCartesian(theta, phi));
            float3 sample = PrefilterEnvMap(gRoughness, R);
            *pPixels      = float4(sample, 1);
            ++pPixels;
        }

        y = GetNextScanline();
    }
}

void ProcessScanlineIrradiance()
{
    int y = GetNextScanline();
    while (y != -1) {
        float4* pPixels = reinterpret_cast<float4*>(gTarget->GetPixels(0, y + gTargetYOffset));

        for (int x = 0; x < gResX; ++x) {
            auto pixel = gIrradianceSource->GetGaussianSample(
                static_cast<float>(x),
                static_cast<float>(y),
                gGaussianKernel,
                BITMAP_SAMPLE_MODE_WRAP,
                BITMAP_SAMPLE_MODE_CLAMP);
            pPixels->r = pixel.r;
            pPixels->g = pixel.g;
            pPixels->b = pixel.b;
            pPixels->a = pixel.a;
            ++pPixels;
        }

        y = GetNextScanline();
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cout << "error: ibl_prefilter_env requires two arguments:" << std::endl;
        std::cout << "   ibl_prefilter_env <input file> <output dir>" << std::endl;
        return EXIT_FAILURE;
    }

    std::filesystem::path inputFilePath = std::filesystem::absolute(argv[1]);
    std::filesystem::path outputDir     = std::filesystem::absolute(argv[2]);
    std::filesystem::path extension     = inputFilePath.extension();
    std::filesystem::path baseFileName  = inputFilePath.filename().replace_extension();

    std::filesystem::path irradianceMapFilePath  = (outputDir / (baseFileName.string() + "_irr")).replace_extension(extension);
    std::filesystem::path environmentMapFilePath = (outputDir / (baseFileName.string() + "_env")).replace_extension(extension);
    std::filesystem::path iblFilePath            = (outputDir / baseFileName).replace_extension("ibl");

    BitmapRGBA32f sourceImage = {};
    if (!BitmapRGBA32f::Load(inputFilePath, &sourceImage)) {
        std::cout << "error: failed to load " << inputFilePath << std::endl;
        return EXIT_FAILURE;
    }

    // Copy source image to start environment map
    gEnvironmentMap = sourceImage;

    // Wide kernel first for irridiance map
    uint32_t radius     = 128;
    uint32_t kernelSize = 2 * radius + 1;
    gGaussianKernel     = GaussianKernel(kernelSize);

    // =========================================================================
    // Irradiance map
    // =========================================================================
    {
        uint32_t      width  = 360;
        uint32_t      height = static_cast<uint32_t>(width / (sourceImage.GetWidth() / static_cast<float>(sourceImage.GetHeight())));
        BitmapRGBA32f target = BitmapRGBA32f(width, height);

        float         scale  = width / static_cast<float>(sourceImage.GetWidth());
        BitmapRGBA32f scaled = sourceImage.Scale(scale, scale, BITMAP_SAMPLE_MODE_WRAP, BITMAP_SAMPLE_MODE_CLAMP);

        gResX             = width;
        gResY             = height;
        gIrradianceSource = &scaled;
        gTarget           = &target;
        gCurrentLevel     = 1;
        gNumLevels        = 2; // Use 2 so that 1/1 gets printed

        // Queue scanlines
        for (int i = 0; i < gResY; ++i) {
            gScanlines.push_back(gResY - i - 1);
        }

        // Launch threads to process scanlines
        std::vector<std::unique_ptr<std::thread>> threads;
        for (int i = 0; i < gNumThreads; ++i) {
            auto thread = std::make_unique<std::thread>(&ProcessScanlineIrradiance);
            threads.push_back(std::move(thread));
        }
        // Wait to threads complete
        for (auto& thread : threads) {
            thread->join();
        }

        if (!target.Empty()) {
            if (!BitmapRGBA32f::Save(irradianceMapFilePath, &target)) {
                std::cout << "error: failed to write " << irradianceMapFilePath << std::endl;
            }

            std::cout << "Successfully wrote " << irradianceMapFilePath << std::endl;
        }
    }

    // Smaller kernel for environment map
    radius          = 3;
    kernelSize      = 2 * radius + 1;
    gGaussianKernel = GaussianKernel(kernelSize);

    // =========================================================================
    // Environemnt map
    // =========================================================================
    gNumLevels = 0;
    {
        // Calculate the number of mip levels and output height
        gNumLevels       = 1;
        int outputHeight = gEnvironmentMap.GetHeight();
        {
            int width  = gEnvironmentMap.GetWidth();
            int height = gEnvironmentMap.GetHeight();
            while (1) {
                width >>= 1;
                height >>= 1;
                //
                // We don't need process anything under 4 pixels
                //
                if ((width < 4) || (height < 4)) {
                    break;
                }
                //
                // We don't need more than 8 levels
                //
                if (gNumLevels >= 8) {
                    break;
                }
                ++gNumLevels;
                // Accumulate output height
                outputHeight += height;
            }
        }
        if (gNumLevels == 0) {
            std::cout << "error: invalid number of mip levels" << std::endl;
            return EXIT_FAILURE;
        }

        BitmapRGBA32f target = BitmapRGBA32f(gEnvironmentMap.GetWidth(), outputHeight);
        gTarget              = &target;

        gResX = static_cast<int>(gEnvironmentMap.GetWidth());
        gResY = static_cast<int>(gEnvironmentMap.GetHeight());

        float deltaRoughness = 1.0f / static_cast<float>(2.0f * gNumLevels);

        for (uint32_t level = 0; level < gNumLevels; ++level) {
            gCurrentLevel = level;

            gDu = 1.0f / static_cast<float>(gResX - 1);
            gDv = 1.0f / static_cast<float>(gResY - 1);

            // Calculate roughness
            gRoughness = std::max<float>(level * deltaRoughness, 0.01f);

            // Queue scanlines
            for (int i = 0; i < gResY; ++i) {
                gScanlines.push_back(i);
            }

            // Launch threads to process scanlines
            std::vector<std::unique_ptr<std::thread>> threads;
            for (int i = 0; i < gNumThreads; ++i) {
                auto thread = std::make_unique<std::thread>(&ProcessScanlineEnvironmentMap);
                threads.push_back(std::move(thread));
            }
            // Wait to complete
            for (auto& thread : threads) {
                thread->join();
            }

            gEnvironmentMap = gTarget->CopyFrom(0, gTargetYOffset, gResX, gResY);

            gTargetYOffset += gResY;

            gResX >>= 1;
            gResY >>= 1;
        }

        if (!target.Empty()) {
            if (!BitmapRGBA32f::Save(environmentMapFilePath, &target)) {
                std::cout << "error: failed to write " << environmentMapFilePath << std::endl;
            }

            std::cout << "Successfully wrote " << environmentMapFilePath << std::endl;
        }
    }

    // =========================================================================
    // IBL file
    // =========================================================================
    {
        std::ofstream os = std::ofstream(iblFilePath.string().c_str());
        os << irradianceMapFilePath.filename() << " " << environmentMapFilePath.filename() << " " << sourceImage.GetWidth() << " " << sourceImage.GetHeight() << " " << gNumLevels << std::endl;
        std::cout << "Successfully wrote " << iblFilePath << std::endl;
    }

    return EXIT_SUCCESS;
}