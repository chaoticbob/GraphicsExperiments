#include "window.h"

#include "mtl_renderer.h"
#include "bitmap.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CHECK_CALL(FN)                                                               \
    {                                                                                \
        NS::Error* pError = FN;                                                      \
        if (pError != nullptr) {                                                     \
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

struct Light
{
    vec3     position;
    uint32_t __pad0;
    vec3     color;
    uint32_t __pad1;
    float    intensity;
    uint32_t __pad2[3];
};

struct SceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t __pad0;
    uint32_t numLights;
    uint32_t __pad1[3];
    Light    lights[8];
    uint     iblEnvNumLevels;
    uint32_t __pad2[3];
};

struct DrawInfo
{
    mat4     modelMatrix;
    uint32_t materialIndex = 0;

    uint32_t    numIndices = 0;
    MetalBuffer indexBuffer;
};

struct DrawParameters
{
    mat4     ModelMatrix;
    uint32_t MaterialIndex;
    uint32_t __pad0[3];
};

struct MaterialParameters
{
    uint32_t UseGeometricNormal;
};

struct MaterialTextures
{
    MetalTexture baseColorTexture;
    MetalTexture normalTexture;
    MetalTexture roughnessTexture;
    MetalTexture metallicTexture;
    MetalTexture aoTexture;
};

struct VertexBuffers
{
    MetalBuffer positionBuffer;
    MetalBuffer texCoordBuffer;
    MetalBuffer normalBuffer;
    MetalBuffer tangentBuffer;
    MetalBuffer bitangentBuffer;
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

float gTargetAngle = 0.0f;
float gAngle       = 0.0f;

static uint32_t gNumLights = 0;

void CreateIBLTextures(
    MetalRenderer* pRenderer,
    MetalTexture*  pBRDFLUT,
    MetalTexture*  pIrradianceTexture,
    MetalTexture*  pEnvironmentTexture,
    uint32_t*      pEnvNumLevels);
void CreateCameraMaterials(
    MetalRenderer*                 pRenderer,
    const TriMesh*                 pMesh,
    const fs::path&                textureDir,
    MetalBuffer*                   pMaterialParamsBuffer,
    MaterialTextures&              outDefaultMaterialTextures,
    std::vector<MaterialTextures>& outMatrialTextures);
void CreateCameraVertexBuffers(
    MetalRenderer*         pRenderer,
    const TriMesh*         pMesh,
    std::vector<DrawInfo>& outDrawParams,
    VertexBuffers&         outVertexBuffers);
void CreateEnvironmentVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pTexCoordBuffer);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT) {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;
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

    if (!InitMetal(renderer.get(), gEnableDebug)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    // PBR shaders
    MetalShader pbrVsShader;
    MetalShader pbrFsShader;
    NS::Error*  pError = nullptr;
    {
        std::string shaderSource = LoadString("projects/202_pbr_camera/shaders.metal");
        if (shaderSource.empty()) {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        auto library = NS::TransferPtr(renderer->Device->newLibrary(
            NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding),
            nullptr,
            &pError));

        if (library.get() == nullptr) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        pbrVsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
        if (pbrVsShader.Function.get() == nullptr) {
            assert(false && "VS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        pbrFsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
        if (pbrFsShader.Function.get() == nullptr) {
            assert(false && "FS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }
    }

    // Draw texture shaders
    MetalShader drawTextureVsShader;
    MetalShader drawTextureFsShader;
    {
        std::string shaderSource = LoadString("projects/202_pbr_camera/drawtexture.metal");
        if (shaderSource.empty()) {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        auto library = NS::TransferPtr(renderer->Device->newLibrary(
            NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding),
            nullptr,
            &pError));

        if (library.get() == nullptr) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        drawTextureVsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
        if (drawTextureVsShader.Function.get() == nullptr) {
            assert(false && "VS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        drawTextureFsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
        if (drawTextureFsShader.Function.get() == nullptr) {
            assert(false && "FS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // PBR pipeline state object
    // *************************************************************************
    MetalPipelineRenderState pbrPipelineState;
    MetalDepthStencilState   pbrDepthStencilState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        &pbrVsShader,
        &pbrFsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pbrPipelineState,
        &pbrDepthStencilState));

    // *************************************************************************
    // Environment pipeline state object
    // *************************************************************************
    MetalPipelineRenderState envPipelineState;
    MetalDepthStencilState   envDepthStencilState;
    CHECK_CALL(CreateDrawTexturePipeline(
        renderer.get(),
        &drawTextureVsShader,
        &drawTextureFsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &envPipelineState,
        &envDepthStencilState));

    // *************************************************************************
    // Load mesh
    // *************************************************************************
    const fs::path           modelDir  = "models/camera";
    const fs::path           modelFile = modelDir / "camera.obj";
    std::unique_ptr<TriMesh> mesh;
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.invertTexCoordsV = true;

        mesh = std::make_unique<TriMesh>(options);
        if (!mesh) {
            assert(false && "allocated mesh failed");
            return EXIT_FAILURE;
        }

        if (!TriMesh::LoadOBJ(GetAssetPath(modelFile).string(), GetAssetPath(modelDir).string(), options, mesh.get())) {
            assert(false && "OBJ load failed");
            return EXIT_FAILURE;
        }

        mesh->Recenter();

        auto bounds = mesh->GetBounds();

        std::stringstream ss;
        ss << "mesh bounding box: "
           << "min = (" << bounds.min.x << ", " << bounds.min.y << ", " << bounds.min.z << ")"
           << " "
           << "max = (" << bounds.max.x << ", " << bounds.max.y << ", " << bounds.max.z << ")";
        GREX_LOG_INFO(ss.str());
    }

    // *************************************************************************
    // Materials
    // *************************************************************************
    MetalBuffer                   materialParamsBuffer;
    MaterialTextures              defaultMaterialTextures = {};
    std::vector<MaterialTextures> materialTexturesSets    = {};
    CreateCameraMaterials(
        renderer.get(),
        mesh.get(),
        GetAssetPath(fs::path(modelDir)),
        &materialParamsBuffer,
        defaultMaterialTextures,
        materialTexturesSets);

    // *************************************************************************
    // Environment texture
    // *************************************************************************
    MetalTexture brdfLUT;
    MetalTexture irrTexture;
    MetalTexture envTexture;
    uint32_t     envNumLevels = 0;
    CreateIBLTextures(renderer.get(), &brdfLUT, &irrTexture, &envTexture, &envNumLevels);

    // *************************************************************************
    // Texture Arrays
    // *************************************************************************
    // Material textures
    std::vector<MTL::Texture*> cameraTextureArray;
    for (auto& materialTextures : materialTexturesSets) {
        // Albedo
        cameraTextureArray.push_back(materialTextures.baseColorTexture.Texture.get());

        // Normal
        cameraTextureArray.push_back(materialTextures.normalTexture.Texture.get());

        // Roughness
        cameraTextureArray.push_back(materialTextures.roughnessTexture.Texture.get());

        // Metalness
        cameraTextureArray.push_back(materialTextures.metallicTexture.Texture.get());

        // Ambient Occlusion
        cameraTextureArray.push_back(materialTextures.aoTexture.Texture.get());
    }

    // *************************************************************************
    // Camera Vertex buffers
    // *************************************************************************
    std::vector<DrawInfo> cameraDrawParams    = {};
    VertexBuffers         cameraVertexBuffers = {};
    CreateCameraVertexBuffers(
        renderer.get(),
        mesh.get(),
        cameraDrawParams,
        cameraVertexBuffers);

    // *************************************************************************
    // Environment vertex buffers
    // *************************************************************************
    uint32_t    envNumIndices = 0;
    MetalBuffer envIndexBuffer;
    MetalBuffer envPositionBuffer;
    MetalBuffer envTexCoordBuffer;
    CreateEnvironmentVertexBuffers(
        renderer.get(),
        &envNumIndices,
        &envIndexBuffer,
        &envPositionBuffer,
        &envTexCoordBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "202_pbr_camera_metal");
    if (!window) {
        assert(false && "GrexWindow::Create failed");
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
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT)) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get())) {
        assert(false && "GrexWindow::InitImGuiForMetal failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents()) {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

        if (ImGui::Begin("Scene")) {
            ImGui::SliderInt("Number of Lights", reinterpret_cast<int*>(&gNumLights), 0, 4);
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable);

        uint32_t swapchainIndex = (frameIndex % renderer->SwapchainBufferCount);
        frameIndex++;

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

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.1f;

        // Camera matrices
        vec3 eyePosition = vec3(0, 4.5f, 8);
        mat4 modelMat    = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));
        mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, -0.25f, 0), vec3(0, 1, 0));
        mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set constant buffer values
        SceneParameters sceneParams      = {};
        sceneParams.viewProjectionMatrix = projMat * viewMat;
        sceneParams.eyePosition          = eyePosition;
        sceneParams.numLights            = gNumLights;
        sceneParams.lights[0].position   = vec3(5, 7, 32);
        sceneParams.lights[0].color      = vec3(1.00f, 0.70f, 0.00f);
        sceneParams.lights[0].intensity  = 0.2f;
        sceneParams.lights[1].position   = vec3(-8, 1, 4);
        sceneParams.lights[1].color      = vec3(1.00f, 0.00f, 0.00f);
        sceneParams.lights[1].intensity  = 0.4f;
        sceneParams.lights[2].position   = vec3(0, 8, -8);
        sceneParams.lights[2].color      = vec3(0.00f, 1.00f, 0.00f);
        sceneParams.lights[2].intensity  = 0.4f;
        sceneParams.lights[3].position   = vec3(15, 8, 0);
        sceneParams.lights[3].color      = vec3(0.00f, 0.00f, 1.00f);
        sceneParams.lights[3].intensity  = 0.4f;
        sceneParams.iblEnvNumLevels      = envNumLevels;

        // Draw environment
        {
            pRenderEncoder->setRenderPipelineState(envPipelineState.State.get());
            pRenderEncoder->setDepthStencilState(envDepthStencilState.State.get());

            glm::mat4 moveUp = glm::translate(vec3(0, 0, 0));

            // DrawParams [[buffer(2)]]
            mat4 mvp = projMat * viewMat * moveUp;
            pRenderEncoder->setVertexBytes(&mvp, sizeof(glm::mat4), 2);

            // Textures
            pRenderEncoder->setFragmentTexture(envTexture.Texture.get(), 2);

            // Vertex buffers
            MTL::Buffer* vbvs[2]    = {};
            NS::UInteger offsets[2] = {};
            NS::Range    vbRange(0, 2);
            // Position
            vbvs[0]    = envPositionBuffer.Buffer.get();
            offsets[0] = 0;
            // Tex coord
            vbvs[1]    = envTexCoordBuffer.Buffer.get();
            offsets[1] = 0;
            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeFront);

            pRenderEncoder->drawIndexedPrimitives(
                MTL::PrimitiveType::PrimitiveTypeTriangle,
                envNumIndices,
                MTL::IndexTypeUInt32,
                envIndexBuffer.Buffer.get(),
                0);
        }

        // Draw camera
        {
            // Vertex Shader Parameters
            // SceneParmas [[buffer(6)]]
            pRenderEncoder->setVertexBytes(&sceneParams, sizeof(SceneParameters), 6);

            // Fragment Shader parameters
            // SceneParams       [[buffer(3)]],
            // MaterialParams    [[buffer(4)]],
            // IBLIntegrationLUT [[texture(0)]],
            // IBLIrradianceMap  [[texture(1)]],
            // IBLEnvironmentMap [[texture(2)]],
            // MaterialTextures  [[texture(3)]])
            pRenderEncoder->setFragmentBytes(&sceneParams, sizeof(SceneParameters), 3);
            pRenderEncoder->setFragmentBuffer(materialParamsBuffer.Buffer.get(), 0, 4);
            pRenderEncoder->setFragmentTexture(brdfLUT.Texture.get(), 0);
            pRenderEncoder->setFragmentTexture(irrTexture.Texture.get(), 1);
            pRenderEncoder->setFragmentTexture(envTexture.Texture.get(), 2);
            pRenderEncoder->setFragmentTextures(DataPtr(cameraTextureArray), NS::Range(3, 10));

            // Vertex buffers
            MTL::Buffer* vbvs[5]    = {};
            NS::UInteger offsets[5] = {};
            NS::Range    vbRange(0, 5);
            // Position
            vbvs[0]    = cameraVertexBuffers.positionBuffer.Buffer.get();
            offsets[0] = 0;
            // TexCoord
            vbvs[1]    = cameraVertexBuffers.texCoordBuffer.Buffer.get();
            offsets[1] = 0;
            // Normal
            vbvs[2]    = cameraVertexBuffers.normalBuffer.Buffer.get();
            offsets[2] = 0;
            // Tangent
            vbvs[3]    = cameraVertexBuffers.tangentBuffer.Buffer.get();
            offsets[3] = 0;
            // Bitangent
            vbvs[4]    = cameraVertexBuffers.bitangentBuffer.Buffer.get();
            offsets[4] = 0;

            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeFront);

            // Pipeline state
            pRenderEncoder->setRenderPipelineState(pbrPipelineState.State.get());
            pRenderEncoder->setDepthStencilState(pbrDepthStencilState.State.get());

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeBack);

            for (auto& draw : cameraDrawParams) {
                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = draw.materialIndex;

                // DrawParams [[buffer(5)]] / [[buffer(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);

                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBuffer(materialParamsBuffer.Buffer.get(), 0, 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    draw.numIndices,
                    MTL::IndexTypeUInt32,
                    draw.indexBuffer.Buffer.get(),
                    0);
            }
        }

        // Draw ImGui
        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateCameraMaterials(
    MetalRenderer*                 pRenderer,
    const TriMesh*                 pMesh,
    const fs::path&                textureDir,
    MetalBuffer*                   pMaterialParamsBuffer,
    MaterialTextures&              outDefaultMaterialTextures,
    std::vector<MaterialTextures>& outMatrialTexturesSets)
{
    // Default material textures
    {
        PixelRGBA8u purplePixel = {0, 0, 0, 255};
        PixelRGBA8u blackPixel  = {0, 0, 0, 255};
        PixelRGBA8u whitePixel  = {255, 255, 255, 255};

        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &purplePixel, &outDefaultMaterialTextures.baseColorTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.normalTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.roughnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.metallicTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &whitePixel, &outDefaultMaterialTextures.aoTexture));
    }

    // Materials
    std::vector<MaterialParameters> materialParamsList;
    for (uint32_t materialIndex = 0; materialIndex < pMesh->GetNumMaterials(); ++materialIndex) {
        auto& material = pMesh->GetMaterial(materialIndex);

        // Material params
        MaterialParameters materialParams = {};
        if (material.name == "LensMaterial") {
            materialParams.UseGeometricNormal = 1;
        }
        materialParamsList.push_back(materialParams);

        // Material textures
        MaterialTextures materialTextures = outDefaultMaterialTextures;
        if (!material.albedoTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.albedoTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (albedo) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                MTL::PixelFormatRGBA8Unorm,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.baseColorTexture));
        }
        if (!material.normalTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.normalTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (normal) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                MTL::PixelFormatRGBA8Unorm,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.normalTexture));
        }
        if (!material.roughnessTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.roughnessTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (roughness) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                MTL::PixelFormatRGBA8Unorm,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.roughnessTexture));
        }
        if (!material.metalnessTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.metalnessTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (metalness) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                MTL::PixelFormatRGBA8Unorm,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.metallicTexture));
        }
        if (!material.aoTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.aoTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (ambient occlusion) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                MTL::PixelFormatRGBA8Unorm,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.aoTexture));
        }

        outMatrialTexturesSets.push_back(materialTextures);
    }

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(materialParamsList),
        DataPtr(materialParamsList),
        pMaterialParamsBuffer));
}

void CreateIBLTextures(
    MetalRenderer* pRenderer,
    MetalTexture*  pBRDFLUT,
    MetalTexture*  pIrradianceTexture,
    MetalTexture*  pEnvironmentTexture,
    uint32_t*      pEnvNumLevels)
{
    // BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
        if (bitmap.Empty()) {
            assert(false && "Load image failed");
            return;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            MTL::PixelFormatRGBA32Float,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pBRDFLUT));
    }

    // IBL file
    auto iblFile = GetAssetPath("IBL/palermo_square_4k.ibl");

    IBLMaps ibl = {};
    if (!LoadIBLMaps32f(iblFile, &ibl)) {
        GREX_LOG_ERROR("failed to load: " << iblFile);
        return;
    }

    *pEnvNumLevels = ibl.numLevels;

    // Irradiance
    {
        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.irradianceMap.GetWidth(),
            ibl.irradianceMap.GetHeight(),
            MTL::PixelFormatRGBA32Float,
            ibl.irradianceMap.GetSizeInBytes(),
            ibl.irradianceMap.GetPixels(),
            pIrradianceTexture));
    }

    // Environment
    {
        const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
        const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

        std::vector<MipOffset> mipOffsets;
        uint32_t               levelOffset = 0;
        uint32_t               levelWidth  = ibl.baseWidth;
        uint32_t               levelHeight = ibl.baseHeight;
        for (uint32_t i = 0; i < ibl.numLevels; ++i) {
            MipOffset mipOffset = {};
            mipOffset.Offset    = levelOffset;
            mipOffset.RowStride = rowStride;

            mipOffsets.push_back(mipOffset);

            levelOffset += (rowStride * levelHeight);
            levelWidth >>= 1;
            levelHeight >>= 1;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.baseWidth,
            ibl.baseHeight,
            MTL::PixelFormatRGBA32Float,
            mipOffsets,
            ibl.environmentMap.GetSizeInBytes(),
            ibl.environmentMap.GetPixels(),
            pEnvironmentTexture));
    }

    GREX_LOG_INFO("Loaded " << iblFile);
}

void CreateCameraVertexBuffers(
    MetalRenderer*         pRenderer,
    const TriMesh*         pMesh,
    std::vector<DrawInfo>& outDrawParams,
    VertexBuffers&         outVertexBuffers)
{
    // Group draws based on material indices
    for (uint32_t materialIndex = 0; materialIndex < pMesh->GetNumMaterials(); ++materialIndex) {
        auto triangles = pMesh->GetTrianglesForMaterial(materialIndex);

        DrawInfo params      = {};
        params.numIndices    = static_cast<uint32_t>(3 * triangles.size());
        params.materialIndex = materialIndex;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(triangles),
            DataPtr(triangles),
            &params.indexBuffer));

        outDrawParams.push_back(params);
    }

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetPositions()),
        DataPtr(pMesh->GetPositions()),
        &outVertexBuffers.positionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetTexCoords()),
        DataPtr(pMesh->GetTexCoords()),
        &outVertexBuffers.texCoordBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetNormals()),
        DataPtr(pMesh->GetNormals()),
        &outVertexBuffers.normalBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetTangents()),
        DataPtr(pMesh->GetTangents()),
        &outVertexBuffers.tangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetBitangents()),
        DataPtr(pMesh->GetBitangents()),
        &outVertexBuffers.bitangentBuffer));
}

void CreateEnvironmentVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pTexCoordBuffer)
{
    TriMesh::Options options;
    options.enableTexCoords = true;
    options.faceInside      = true;

    TriMesh mesh = TriMesh::Sphere(100, 64, 64, options);

    *pNumIndices = 3 * mesh.GetNumTriangles();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        pIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        pTexCoordBuffer));
}
