//
// normal_map_convert
// - Converts a linear normal map from 32-bit or 16-bit floating point to 8-bit unsigned int without applying gamma correction
//

#include <algorithm>
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

#include "bitmap.h"

#include "pcg32.h"

// std::string ToLowerCaseCopy(std::string s)
//{
//     std::transform(
//         s.begin(),
//         s.end(),
//         s.begin(),
//         [](std::string::value_type c) { return std::tolower(c); });
//     return s;
// }

using float2 = glm::vec2;
using float3 = glm::vec3;

float saturate(float x)
{
    return glm::clamp(x, 0.0f, 1.0f);
}

int main(int argc, char** argv)
{
    const uint32_t kMaxWidth  = 8192;
    const uint32_t kMaxHeight = 8192;

    if (argc < 3)
    {
        std::cout << "error: missing arguments" << std::endl;
        std::cout << "   "
                  << "normal_map_convert <input file> <output file> [optional:flags/options]" << std::endl;
        std::cout << "\nEx:\n";
        std::cout << "   "
                  << "normal_map_convert normal_map.exr normal_map.png" << std::endl;
        std::cout << "\n\n";
        std::cout << "Flags and options:\n";
        std::cout << "   -w <value>   Ouptut width\n";
        std::cout << "   -h <value>   Output height\n";
        std::cout << std::endl;
        return EXIT_FAILURE;
    }

    std::filesystem::path inputFile  = argv[1];
    std::filesystem::path outputFile = argv[2];

    uint32_t    outputWidth  = 0;
    uint32_t    outputHeight = 0;
    std::string badOption    = "";
    for (int i = 3; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-w")
        {
            ++i;
            if (i >= argc)
            {
                badOption = arg;
                break;
            }
            outputWidth = static_cast<uint32_t>(atoi(argv[i]));
        }
        else if (arg == "-h")
        {
            ++i;
            if (i >= argc)
            {
                badOption = arg;
                break;
            }
            outputHeight = static_cast<uint32_t>(atoi(argv[i]));
        }
        else
        {
            std::cout << "error: unrecognized arg " << arg << std::endl;
            return EXIT_FAILURE;
        }
    }
    if (!badOption.empty())
    {
        std::cout << "error: missing arg for option " << badOption << std::endl;
        return EXIT_FAILURE;
    }

    if (outputWidth > kMaxWidth)
    {
        std::cout << "error: width is too big" << std::endl;
        std::cout << "max width is " << kMaxWidth << std::endl;
        return EXIT_FAILURE;
    }

    if (outputHeight > kMaxHeight)
    {
        std::cout << "error: height is too big" << std::endl;
        std::cout << "max height is " << kMaxHeight << std::endl;
        return EXIT_FAILURE;
    }

    if (!std::filesystem::exists(inputFile))
    {
        std::cout << "error: input file does not exist " << inputFile << std::endl;
        return EXIT_FAILURE;
    }

    auto absInputFile  = std::filesystem::absolute(inputFile);
    auto absOutputFile = std::filesystem::absolute(outputFile);
    if (absInputFile == absOutputFile)
    {
        std::cout << "error: input file and output file must be different " << inputFile << std::endl;
        return EXIT_FAILURE;
    }

    BitmapRGBA32f inputBitmap;
    if (!BitmapRGBA32f::Load(inputFile, &inputBitmap))
    {
        std::cout << "error: failed to load input file " << inputFile << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "Successfully loaded " << inputBitmap.GetWidth() << "x" << inputBitmap.GetHeight() << " " << inputFile << std::endl;

    BitmapRGBA8u outputBitmap = BitmapRGBA8u(inputBitmap.GetWidth(), inputBitmap.GetHeight());
    if (outputBitmap.Empty())
    {
        std::cout << "error: output bitmap memory allocation failed" << std::endl;
        return EXIT_FAILURE;
    }

    pcg32 random = pcg32(0xDEADBEEF);

    auto pSrcPixels = inputBitmap.GetPixels();
    auto pDstPixels = outputBitmap.GetPixels();
    for (uint32_t row = 0; row < inputBitmap.GetHeight(); ++row)
    {
        for (uint32_t col = 0; col < inputBitmap.GetWidth(); ++col)
        {
            float r       = (255.0f * pSrcPixels->r);
            float g       = (255.0f * pSrcPixels->g);
            float b       = (255.0f * pSrcPixels->b);
            float a       = (255.0f * pSrcPixels->a);
            pDstPixels->r = static_cast<uint8_t>(r);
            pDstPixels->g = static_cast<uint8_t>(g);
            pDstPixels->b = static_cast<uint8_t>(b);
            pDstPixels->a = static_cast<uint8_t>(a);
            ++pSrcPixels;
            ++pDstPixels;
        }
    }
    std::cout << "Converted to 8-bit unsigned int linear" << std::endl;

    // Calculate width or height if needed
    BitmapRGBA8u scaledOutputBitmap;
    if ((outputWidth > 0) || (outputHeight > 0))
    {
        float aspect = outputBitmap.GetWidth() / static_cast<float>(outputBitmap.GetHeight());
        if ((outputWidth > 0) && (outputHeight == 0))
        {
            outputHeight = static_cast<uint32_t>(outputWidth / aspect);
        }
        else if ((outputHeight > 0) && (outputWidth == 0))
        {
            outputWidth = static_cast<uint32_t>(outputHeight * aspect);
        }

        scaledOutputBitmap.Resize(outputWidth, outputHeight);
        outputBitmap.ScaleTo(BITMAP_SAMPLE_MODE_WRAP, BITMAP_SAMPLE_MODE_WRAP, BITMAP_FILTER_MODE_GAUSSIAN, scaledOutputBitmap);
        outputBitmap = scaledOutputBitmap;
    }

    if (!BitmapRGBA8u::Save(outputFile, &outputBitmap))
    {
        std::cout << "error: failed to write output file " << outputFile << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Successfully wrote output file " << outputBitmap.GetWidth() << "x" << outputBitmap.GetHeight() << " " << outputFile << std::endl;

    return EXIT_SUCCESS;
}