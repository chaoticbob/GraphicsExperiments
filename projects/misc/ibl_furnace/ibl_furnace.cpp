
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <glm/glm.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cout << "error: missing arguments" << std::endl;
        std::cout << "   "
                  << "ibl_furnance <output file> <width> <height>" << std::endl;
        std::cout << "\nEx:\n";
        std::cout << "   "
                  << "ibl_furnance furnace.hdr 2048 1024" << std::endl;
        return EXIT_FAILURE;
    }

    std::filesystem::path outputPath = argv[1];

    const uint32_t kMaxWidth  = 8192;
    const uint32_t kMaxHeight = 4096;

    uint32_t width = static_cast<uint32_t>(atoi(argv[2]));
    if (width > kMaxWidth) {
        std::cout << "error: width is too big" << std::endl;
        std::cout << "max width is " << kMaxWidth << std::endl;
        return EXIT_FAILURE;
    }

    uint32_t height = static_cast<uint32_t>(atoi(argv[3]));
    if (height > kMaxHeight) {
        std::cout << "error: height is too big" << std::endl;
        std::cout << "max height is " << kMaxHeight << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<glm::vec4> pixels(width * height);
    std::fill(pixels.begin(), pixels.end(), glm::vec4(2.0f));

    if (!pixels.empty()) {
        int res = stbi_write_hdr(outputPath.string().c_str(), width, height, 3, reinterpret_cast<const float*>(pixels.data()));
        if (res == 0) {
            std::cout << "ERROR: failed to write " << outputPath << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "Successfully wrote furnace to " << outputPath << std::endl;
    }

    return EXIT_SUCCESS;
}