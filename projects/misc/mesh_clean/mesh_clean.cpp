
#include <filesystem>
#include <iostream>
#include <string>

#include "tri_mesh.h"
#include "tiny_obj_loader.h"

#include "meshoptimizer.h"

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "error: missing params\n"
                  << std::endl;
        std::cout << "usage:\n  mesh_clean input.obj output.obj" << std::endl;
        return EXIT_FAILURE;
    }

    auto inputPath = std::filesystem::path(argv[1]);
    if (!std::filesystem::exists(inputPath))
    {
        std::cout << "error: input path does not exist\n   input=" << inputPath << std::endl;
        return EXIT_FAILURE;
    }

    auto outputPath = std::filesystem::path(argv[2]);
    if (std::filesystem::path(inputPath) == std::filesystem::path(outputPath))
    {
        std::cout << "error: input path and output path cannot be the same\n  input=" << inputPath << "\n  output=" << outputPath << std::endl;
        return EXIT_FAILURE;
    }

    TriMesh::Options options = {};
    options.enableTexCoords  = true;
    options.enableNormals    = true;

    TriMesh inputMesh = {};
    bool    res       = TriMesh::LoadOBJ(inputPath.string(), "", options, &inputMesh);
    if (!res)
    {
        std::cout << "error: failed to load input\n   input=" << inputPath << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "loaded " << inputPath << std::endl;
    std::cout << std::endl;

    std::cout << "initial values" << std::endl;
    std::cout << "num vertices: " << inputMesh.GetNumVertices() << std::endl;
    std::cout << "num indices : " << inputMesh.GetNumIndices() << std::endl;

    std::cout << std::endl;
    std::cout << "weldinging vertices..." << std::endl;
    inputMesh.WeldVertices();
    std::cout << "num vertices: " << inputMesh.GetNumVertices() << std::endl;
    std::cout << "num indices : " << inputMesh.GetNumIndices() << std::endl;

    std::cout << std::endl;
    std::cout << "spatially sorting triangles..." << std::endl;
    {
        const auto indices = inputMesh.GetIndices();

        std::vector<uint32_t> sortedIndices(inputMesh.GetNumIndices());
        meshopt_spatialSortTriangles(
            sortedIndices.data(),
            indices.data(),
            static_cast<uint32_t>(indices.size()),
            reinterpret_cast<const float*>(inputMesh.GetPositions().data()),
            inputMesh.GetNumVertices(),
            sizeof(glm::vec3));

        inputMesh.SetTriangles(sortedIndices);
    }
    std::cout << "spatial sorting complete" << std::endl;

    TriMesh::WriteOBJ(outputPath.string(), inputMesh);
    std::cout << std::endl;
    std::cout << "wrote " << outputPath << std::endl;

    return EXIT_SUCCESS;
}