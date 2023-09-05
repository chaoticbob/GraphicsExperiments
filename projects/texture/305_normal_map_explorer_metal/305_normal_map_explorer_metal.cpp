#include "window.h"

#include "mtl_renderer.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CHECK_CALL(FN)                                                               \
    {                                                                                \
        NS::Error* pError = FN;                                                      \
        if (pError != nullptr)                                                       \
        {                                                                            \
            std::stringstream ss;                                                    \
            ss << "\n";                                                              \
            ss << "*** FUNCTION CALL FAILED *** \n";                                 \
            ss << "FUNCTION: " << #FN << "\n";                                       \
            ss << "Error: " << pError->localizedDescription()->utf8String() << "\n"; \
            ss << "\n";                                                              \
            GREX_LOG_ERROR(ss.str().c_str());                                        \
            assert(false);                                                           \
        }                                                                            \
    }

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngleX = 0.0f;
static float gAngleX       = 0.0f;
static float gTargetAngleY = 0.0f;
static float gAngleY       = 0.0f;

struct TextureSet
{
    std::string  name;
    MetalTexture diffuseTexture;
    MetalTexture normalTexture;
};

struct Geometry
{
    std::string name;
    MetalBuffer indexBuffer;
    uint32_t    numIndices;
    MetalBuffer positionBuffer;
    MetalBuffer texCoordBuffer;
    MetalBuffer normalBuffer;
    MetalBuffer tangentBuffer;
    MetalBuffer bitangentBuffer;
};

struct CameraProperties
{
    mat4     ModelMatrix;
    mat4     ViewProjectionMatrix;
    vec3     EyePosition;
    uint32_t _pad0;
};

void CreateTextureSets(
    MetalRenderer*           pRenderer,
    std::vector<TextureSet>& outTextureSets);
void CreateGeometryBuffers(
    MetalRenderer*         pRenderer,
    std::vector<Geometry>& outGeometries);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    int dx = x - prevX;
    int dy = y - prevY;

    if (buttons & MOUSE_BUTTON_RIGHT)
    {
        gTargetAngleX += 0.25f * dy;
    }
    if (buttons & MOUSE_BUTTON_LEFT)
    {
        gTargetAngleY += 0.25f * dx;
    }

    prevX = x;
    prevY = y;
}

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<MetalRenderer> renderer = std::make_unique<MetalRenderer>();

    if (!InitMetal(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::string shaderSource = LoadString("projects/305_normal_map_explorer/shaders.metal");

    MetalShader vsShader;
    MetalShader fsShader;
    NS::Error*  pError  = nullptr;
    auto        library = NS::TransferPtr(renderer->Device->newLibrary(
        NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding),
        nullptr,
        &pError));

    if (library.get() == nullptr)
    {
        std::stringstream ss;
        ss << "\n"
           << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
        return EXIT_FAILURE;
    }

    vsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
    if (vsShader.Function.get() == nullptr)
    {
        assert(false && "VS Shader MTL::Library::newFunction() failed");
        return EXIT_FAILURE;
    }

    fsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
    if (fsShader.Function.get() == nullptr)
    {
        assert(false && "FS Shader MTL::Library::newFunction() failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    MetalPipelineRenderState renderPipelineState;
    MetalDepthStencilState   depthStencilState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &renderPipelineState,
        &depthStencilState));

    // *************************************************************************
    // Texture
    // *************************************************************************
    std::vector<TextureSet> textureSets;
    CreateTextureSets(renderer.get(), textureSets);

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    std::vector<Geometry> geometries;
    CreateGeometryBuffers(renderer.get(), geometries);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "305_normal_map_explorer_metal");
    if (!window)
    {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Render Pass Description
    // *************************************************************************
    MTL::RenderPassDescriptor* pRenderPassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get()))
    {
        assert(false && "Window::InitImGuiForMetal failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Misc vars
    // *************************************************************************
    uint32_t textureSetIndex        = 0;
    uint32_t currentTextureSetIndex = ~0;
    uint32_t geoIndex               = 0;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents())
    {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);
        if (ImGui::Begin("Scene"))
        {
            static const char* currentTextureSetName = textureSets[0].name.c_str();
            if (ImGui::BeginCombo("Textures", currentTextureSetName))
            {
                for (size_t i = 0; i < textureSets.size(); ++i)
                {
                    bool isSelected = (currentTextureSetName == textureSets[i].name);
                    if (ImGui::Selectable(textureSets[i].name.c_str(), isSelected))
                    {
                        currentTextureSetName = textureSets[i].name.c_str();
                        textureSetIndex       = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            static const char* currentGeoName = geometries[0].name.c_str();
            if (ImGui::BeginCombo("Geometry", currentGeoName))
            {
                for (size_t i = 0; i < geometries.size(); ++i)
                {
                    bool isSelected = (currentGeoName == geometries[i].name);
                    if (ImGui::Selectable(geometries[i].name.c_str(), isSelected))
                    {
                        currentGeoName = geometries[i].name.c_str();
                        geoIndex       = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::End();

        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

        uint32_t swapchainIndex = (frameIndex % renderer->SwapchainBufferCount);

        auto colorTargetDesc = NS::TransferPtr(MTL::RenderPassColorAttachmentDescriptor::alloc()->init());
        colorTargetDesc->setClearColor(clearColor);
        colorTargetDesc->setTexture(pDrawable->texture());
        colorTargetDesc->setLoadAction(MTL::LoadActionClear);
        colorTargetDesc->setStoreAction(MTL::StoreActionStore);
        pRenderPassDescriptor->colorAttachments()->setObject(colorTargetDesc.get(), 0);

        auto depthTargetDesc = NS::TransferPtr(MTL::RenderPassDepthAttachmentDescriptor::alloc()->init());
        depthTargetDesc->setClearDepth(1);
        depthTargetDesc->setTexture(renderer->SwapchainDSVBuffers[swapchainIndex].get());
        depthTargetDesc->setLoadAction(MTL::LoadActionClear);
        depthTargetDesc->setStoreAction(MTL::StoreActionDontCare);
        pRenderPassDescriptor->setDepthAttachment(depthTargetDesc.get());

        MTL::CommandBuffer*        pCommandBuffer = renderer->Queue->commandBuffer();
        MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);

        pRenderEncoder->setRenderPipelineState(renderPipelineState.State.get());
        pRenderEncoder->setDepthStencilState(depthStencilState.State.get());

        // Smooth out the rotation
        gAngleX += (gTargetAngleX - gAngleX) * 0.1f;
        gAngleY += (gTargetAngleY - gAngleY) * 0.1f;

        mat4 modelMat = glm::rotate(glm::radians(gAngleY), vec3(0, 1, 0)) *
                        glm::rotate(glm::radians(gAngleX), vec3(1, 0, 0));

        vec3 eyePos      = vec3(0, 1.0f, 1.25f);
        mat4 viewMat     = lookAt(eyePos, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat     = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
        mat4 projViewMat = projMat * viewMat;

        CameraProperties cam     = {};
        cam.ModelMatrix          = modelMat;
        cam.ViewProjectionMatrix = projViewMat;
        cam.EyePosition          = eyePos;

        pRenderEncoder->setVertexBytes(&cam, sizeof(CameraProperties), 5);
        pRenderEncoder->setFragmentBytes(&cam, sizeof(CameraProperties), 5);

        if (currentTextureSetIndex != textureSetIndex)
        {
            currentTextureSetIndex = textureSetIndex;
        }

        auto& textureSet = textureSets[currentTextureSetIndex];
        pRenderEncoder->setFragmentTexture(textureSet.diffuseTexture.Texture.get(), 0);
        pRenderEncoder->setFragmentTexture(textureSet.normalTexture.Texture.get(), 1);

        auto& geo = geometries[geoIndex];

        MTL::Buffer* vbvs[5] = {
            geo.positionBuffer.Buffer.get(),
            geo.texCoordBuffer.Buffer.get(),
            geo.normalBuffer.Buffer.get(),
            geo.tangentBuffer.Buffer.get(),
            geo.bitangentBuffer.Buffer.get()};

        NS::UInteger offsets[5] = {0, 0, 0};
        NS::Range    vbRange(0, 5);
        pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

        pRenderEncoder->drawIndexedPrimitives(
            MTL::PrimitiveType::PrimitiveTypeTriangle,
            geo.numIndices,
            MTL::IndexTypeUInt32,
            geo.indexBuffer.Buffer.get(),
            0);

        // Draw ImGui
        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateTextureSets(
    MetalRenderer*           pRenderer,
    std::vector<TextureSet>& outTextureSets)
{
    // Texture dir
    auto texturesDir = GetAssetPath("textures");

    // Get material files
    std::vector<std::filesystem::path> materialFiles;
    for (auto& entry : std::filesystem::directory_iterator(texturesDir))
    {
        if (!entry.is_directory())
        {
            continue;
        }
        auto materialFilePath = entry.path() / "material.mat";
        if (!fs::exists(materialFilePath))
        {
            continue;
        }
        materialFiles.push_back(materialFilePath);
    }

    // Sort the paths so we match functionality on Windows
    std::sort(materialFiles.begin(), materialFiles.end());

    size_t maxEntries = materialFiles.size();
    for (size_t i = 0; i < maxEntries; ++i)
    {
        auto materialFile = materialFiles[i];

        std::ifstream is = std::ifstream(materialFile.string().c_str());
        if (!is.is_open())
        {
            assert(false && "faild to open material file");
        }

        TextureSet textureSet = {};
        textureSet.name       = materialFile.parent_path().filename().string();

        while (!is.eof())
        {
            MetalTexture*         pTargetTexture = nullptr;
            std::filesystem::path textureFile    = "";

            std::string key;
            is >> key;
            if (key == "basecolor")
            {
                is >> textureFile;
                pTargetTexture = &textureSet.diffuseTexture;
            }
            else if (key == "normal")
            {
                is >> textureFile;
                pTargetTexture = &textureSet.normalTexture;
            }

            if (textureFile.empty())
            {
                continue;
            }

            auto cwd    = materialFile.parent_path().filename();
            textureFile = "textures" / cwd / textureFile;

            auto bitmap = LoadImage8u(textureFile);
            if (!bitmap.Empty())
            {
                MipmapRGBA8u mipmap = MipmapRGBA8u(
                    bitmap,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_FILTER_MODE_NEAREST);

                std::vector<MipOffset> mipOffsets;
                for (auto& srcOffset : mipmap.GetOffsets())
                {
                    MipOffset dstOffset = {};
                    dstOffset.Offset    = srcOffset;
                    dstOffset.RowStride = mipmap.GetRowStride();
                    mipOffsets.push_back(dstOffset);
                }

                CHECK_CALL(CreateTexture(
                    pRenderer,
                    mipmap.GetWidth(0),
                    mipmap.GetHeight(0),
                    MTL::PixelFormatRGBA8Unorm,
                    mipOffsets,
                    mipmap.GetSizeInBytes(),
                    mipmap.GetPixels(),
                    pTargetTexture));

                GREX_LOG_INFO("Created texture from " << textureFile);
            }
            else
            {
                GREX_LOG_ERROR("Failed to load: " << textureFile);
                assert(false && "Failed to load texture!");
            }
        }

        outTextureSets.push_back(textureSet);
    }

    if (outTextureSets.empty())
    {
        assert(false && "No textures!");
    }
}

void CreateGeometryBuffers(
    MetalRenderer*         pRenderer,
    std::vector<Geometry>& outGeometries)
{
    TriMesh::Options options;
    options.enableTexCoords = true;
    options.enableNormals   = true;
    options.enableTangents  = true;

    std::vector<TriMesh> meshes;
    // Cube
    {
        Geometry geometry = {.name = "Cube"};
        outGeometries.push_back(geometry);

        TriMesh mesh = TriMesh::Cube(vec3(1), false, options);
        meshes.push_back(mesh);
    }

    // Sphere
    {
        Geometry geometry = {.name = "Sphere"};
        outGeometries.push_back(geometry);

        TriMesh mesh = TriMesh::Sphere(0.5f, 64, 32, options);
        meshes.push_back(mesh);
    }

    // Plane
    {
        Geometry geometry = {.name = "Plane"};
        outGeometries.push_back(geometry);

        TriMesh mesh = TriMesh::Plane(vec2(1.5f), 1, 1, vec3(0, 1, 0), options);
        meshes.push_back(mesh);
    }

    // Material Knob
    {
        Geometry geometry = {.name = "Material Knob"};
        outGeometries.push_back(geometry);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh))
        {
            assert(false && "Failed to load material knob");
        }
        mesh.ScaleToFit(0.75f);
        meshes.push_back(mesh);
    }

    // Monkey
    {
        Geometry geometry = {.name = "Monkey"};
        outGeometries.push_back(geometry);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/monkey.obj").string(), "", options, &mesh))
        {
            assert(false && "Failed to load material knob");
        }
        mesh.ScaleToFit(0.75f);
        meshes.push_back(mesh);
    }

    for (size_t i = 0; i < meshes.size(); ++i)
    {
        auto& mesh     = meshes[i];
        auto& geometry = outGeometries[i];

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &geometry.indexBuffer));

        geometry.numIndices = mesh.GetNumIndices();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &geometry.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &geometry.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &geometry.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &geometry.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &geometry.bitangentBuffer));
    }
}
