
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
using namespace glm;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define PI 3.1415926535897932384626433832795f

using float2 = glm::vec2;
using float3 = glm::vec3;

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

/*
//
// From the filament docs. Geometric Shadowing function
// https://google.github.io/filament/Filament.html#toc4.4.2
//
float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
    float a2   = glm::pow(roughness, 4.0f);
    float GGXV = NoL * sqrt(NoV * NoV * (1.0f - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / (GGXV + GGXL);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0f;

    float nom   = NdotV;
    float denom = NdotV * (1.0f - k) + k;

    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = glm::max(dot(N, V), 0.0f);
    float NdotL = glm::max(dot(N, L), 0.0f);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
*/

//
// GGX and Schlick-Beckmann
//
float geometry(float cosTheta, float k)
{
    return (cosTheta) / (cosTheta * (1.0f - k) + k);
}

//
// Geometry for IBL uses a different k than direct lighting
//
float smithsIBL(float NdotV, float NdotL, float roughness)
{
    float k = (roughness * roughness) / 2.0f;

    return geometry(NdotV, k) * geometry(NdotL, k);
}

// DEFAULT - supports Geometry_Smiths
// Schlick-Beckmann (https://www.shadertoy.com/view/3tlBW7)
//
float Geometry_SchlickBeckman(float NoV, float k)
{
    return NoV / (NoV * (1.0f - k) + k);
}

// DEFAULT
// Smiths (https://www.shadertoy.com/view/3tlBW7)
//
float Geometry_Smiths(float NoV, float NoL, float roughness)
{
    //
    // NOTE: Geometry for IBL uses a different k than direct lighting
    //
    float k  = (roughness * roughness) / 2.0f;
    float G1 = Geometry_SchlickBeckman(NoV, k);
    float G2 = Geometry_SchlickBeckman(NoL, k);
    return G1 * G2;
}

float2 IntegrateBRDF(float Roughness, float NoV)
{
    float3 V = float3(0);
    V.x      = sqrt(1.0f - NoV * NoV); // sin
    V.y      = 0;
    V.z      = NoV; // cos
    float A  = 0;
    float B  = 0;

    float3 N = float3(0, 0, 1);

    const uint NumSamples = 1024;
    for (uint i = 0; i < NumSamples; i++) {
        float2 Xi  = Hammersley(i, NumSamples);
        float3 H   = ImportanceSampleGGX(Xi, Roughness, N);
        float3 L   = 2 * dot(V, H) * H - V;
        float  NoL = saturate(L.z);
        float  NoH = saturate(H.z);
        float  VoH = saturate(dot(V, H));
        if (NoL > 0) {
            float G     = Geometry_Smiths(NoV, NoL, Roughness);
            float G_Vis = G * VoH / (NoH * NoV);
            float Fc    = glm::pow(1 - VoH, 5.0f);
            A += (1 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    float2 res = float2(A, B) / float2(NumSamples);
    return res;
}

float2 IntegrateBRDF_Multiscatter(float Roughness, float NoV)
{
    float3 V = float3(0);
    V.x      = sqrt(1.0f - NoV * NoV); // sin
    V.y      = 0;
    V.z      = NoV; // cos
    float A  = 0;
    float B  = 0;

    float3 N = float3(0, 0, 1);

    const uint NumSamples = 1024;
    for (uint i = 0; i < NumSamples; i++) {
        float2 Xi  = Hammersley(i, NumSamples);
        float3 H   = ImportanceSampleGGX(Xi, Roughness, N);
        float3 L   = 2 * dot(V, H) * H - V;
        float  NoL = saturate(L.z);
        float  NoH = saturate(H.z);
        float  VoH = saturate(dot(V, H));
        if (NoL > 0) {
            float G     = Geometry_Smiths(NoV, NoL, Roughness);
            float G_Vis = G * VoH / (NoH * NoV);
            float Fc    = glm::pow(1 - VoH, 5.0f);
            A += G_Vis * Fc;
            B += G_Vis;
        }
    }

    float2 res = float2(A, B) / float2(NumSamples);
    return res;
}

// =============================================================================
// Adapted from Krzysztof Narkowicz:
//   https://github.com/knarkowicz/IntegrateDFG/blob/master/main.cpp
// =============================================================================

uint32_t ReverseBits(uint32_t v)
{
    v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
    v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
    v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
    v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
    v = (v >> 16) | (v << 16);
    return v;
}

float Vis(float roughness, float ndotv, float ndotl)
{
    // GSmith correlated
    float m    = roughness * roughness;
    float m2   = m * m;
    float visV = ndotl * sqrt(ndotv * (ndotv - ndotv * m2) + m2);
    float visL = ndotv * sqrt(ndotl * (ndotl - ndotl * m2) + m2);
    return 0.5f / (visV + visL);
}

float2 IntegrateBRDF_Narkowicz(int x, float ndotv, int LUT_WIDTH)
{
    const unsigned int sampleNum = 1; // 512;

    float const roughness = (x + 0.5f) / LUT_WIDTH;
    float const m         = roughness * roughness;
    float const m2        = m * m;

    float const vx = sqrtf(1.0f - ndotv * ndotv);
    float const vy = 0.0f;
    float const vz = ndotv;

    float scale = 0.0f;
    float bias  = 0.0f;

    for (unsigned int i = 0; i < sampleNum; ++i) {
        float const e1 = (float)i / sampleNum;
        float const e2 = (float)((double)ReverseBits(i) / (double)0x100000000LL);

        float const phi      = 2.0f * PI * e1;
        float const cosPhi   = cosf(phi);
        float const sinPhi   = sinf(phi);
        float const cosTheta = sqrtf((1.0f - e2) / (1.0f + (m2 - 1.0f) * e2));
        float const sinTheta = sqrtf(1.0f - cosTheta * cosTheta);

        float const hx = sinTheta * cosf(phi);
        float const hy = sinTheta * sinf(phi);
        float const hz = cosTheta;

        float const vdh = vx * hx + vy * hy + vz * hz;
        float const lx  = 2.0f * vdh * hx - vx;
        float const ly  = 2.0f * vdh * hy - vy;
        float const lz  = 2.0f * vdh * hz - vz;

        float const ndotl = std::max(lz, 0.0f);
        float const ndoth = std::max(hz, 0.0f);
        float const vdoth = std::max(vdh, 0.0f);

        if (ndotl > 0.0f) {
            float const vis         = Vis(roughness, ndotv, ndotl);
            float const ndotlVisPDF = ndotl * vis * (4.0f * vdoth / ndoth);
            float const fresnel     = powf(1.0f - vdoth, 5.0f);

            scale += ndotlVisPDF * (1.0f - fresnel);
            bias += ndotlVisPDF * fresnel;
        }
    }
    scale /= sampleNum;
    bias /= sampleNum;

    return float2(scale, bias);
}

// =============================================================================
// Main
// =============================================================================

int                 gNumThreads = 64;
int                 gResX       = 0;
int                 gResY       = 0;
std::vector<int>    gScanlines;
std::mutex          gScanlineMutex;
std::vector<float3> gPixels;
bool                gMultiscatter = false;

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
            std::cout << "Procssing:  " << std::fixed << std::setw(4) << std::setprecision(2) << percent << "% complete" << std::endl;
        }
    }

    return scanline;
}

void ProcessScaline()
{
    int y = GetNextScanline();
    while (y != -1) {
        float3* pPixels = &gPixels[y * gResX];

        for (int x = 0; x < gResX; ++x) {
            float  roughness = (static_cast<float>(x) + 0.5f) / static_cast<float>(gResX);
            float  NoV       = (static_cast<float>(y) + 0.5f) / static_cast<float>(gResY);
            float2 brdf      = float2(0, 0);
            if (gMultiscatter) {
                brdf = IntegrateBRDF_Multiscatter(roughness, NoV);
            }
            else {
                brdf = IntegrateBRDF(roughness, NoV);
            }
            *pPixels = float3(brdf, 0);
            ++pPixels;
        }

        //
        // Alternative version using Krzysztof Narkowicz's implementation
        //
        // int         LUT_WIDTH  = gResX;
        // int         LUT_HEIGHT = gResY;
        // const float ndotv      = (y + 0.5f) / static_cast<float>(LUT_HEIGHT);
        // for (int x = 0; x < gResX; ++x) {
        //    float2 brdf = IntegrateBRDF_Narkowicz(x, ndotv, LUT_WIDTH);
        //    *pPixels    = float3(brdf, 0);
        //    ++pPixels;
        // }
        //

        y = GetNextScanline();
    }
}

int main(int argc, char** argv)
{
    const uint32_t kMaxWidth  = 8192;
    const uint32_t kMaxHeight = 8192;

    if (argc < 2) {
        std::cout << "error: missing arguments" << std::endl;
        std::cout << "   "
                  << "ibl_brdf_lut <output file> [optional:flags/options]" << std::endl;
        std::cout << "\nEx:\n";
        std::cout << "   "
                  << "ibl_brdf_lut brdf_lut.hdr" << std::endl;
        std::cout << "\n\n";
        std::cout << "Flags and options:\n";
        std::cout << "   -w <value>   LUT width\n";
        std::cout << "   -h <value>   LUT height\n";
        std::cout << "   -ms          Multiscatter\n";
        std::cout << std::endl;
        return EXIT_FAILURE;
    }

    std::filesystem::path outputFile = argv[1];
    uint32_t              width      = 1024;
    uint32_t              height     = 1024;

    std::string badOption = "";
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-w") {
            ++i;
            if (i >= argc) {
                badOption = arg;
                break;
            }
            width = static_cast<uint32_t>(atoi(argv[i]));
        }
        else if (arg == "-h") {
            ++i;
            if (i >= argc) {
                badOption = arg;
                break;
            }
            height = static_cast<uint32_t>(atoi(argv[i]));
        }
        else if (arg == "-ms") {
            gMultiscatter = true;
        }
        else {
            std::cout << "error: unrecognized arg " << arg << std::endl;
            return EXIT_FAILURE;
        }
    }
    if (!badOption.empty()) {
        std::cout << "error: missing arg for option " << badOption << std::endl;
        return EXIT_FAILURE;
    }

    if (width > kMaxWidth) {
        std::cout << "error: width is too big" << std::endl;
        std::cout << "max width is " << kMaxWidth << std::endl;
        return EXIT_FAILURE;
    }

    if (height > kMaxHeight) {
        std::cout << "error: height is too big" << std::endl;
        std::cout << "max height is " << kMaxHeight << std::endl;
        return EXIT_FAILURE;
    }

    gResX = width;
    gResY = height;

    gPixels.resize(gResX * gResY);

    for (int i = 0; i < gResY; ++i) {
        gScanlines.push_back(gResY - i - 1);
    }

    std::vector<std::unique_ptr<std::thread>> threads;
    for (int i = 0; i < gNumThreads; ++i) {
        auto thread = std::make_unique<std::thread>(&ProcessScaline);
        threads.push_back(std::move(thread));
    }

    for (auto& thread : threads) {
        thread->join();
    }

    if (!gPixels.empty()) {
        int res = stbi_write_hdr(outputFile.string().c_str(), gResX, gResY, 3, reinterpret_cast<const float*>(gPixels.data()));
        if (res == 0) {
            std::cout << "ERROR: failed to write " << outputFile << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "Successfully wrote " << gResX << "x" << gResY <<  (gMultiscatter ? " multiscatter" : "") << " BRDF LUT to " << outputFile << std::endl;
    }

    return EXIT_SUCCESS;
}