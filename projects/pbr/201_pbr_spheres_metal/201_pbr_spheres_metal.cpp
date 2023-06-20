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
    uint     iblEnvironmentNumLevels;
    uint32_t __pad2[3];
};

struct MaterialParameters
{
    vec3     baseColor;
    uint32_t __pad0;
    float    roughness;
    float    metallic;
    uint32_t __pad1[2];
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 1024;
static bool     gEnableDebug  = true;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static uint32_t gNumLights = 0;

void CreateMaterialSphereVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pNormalBuffer);
void CreateEnvironmentVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pTexCoordBuffer);
void CreateIBLTextures(
    MetalRenderer* pRenderer,
    MetalTexture*  pBRDFLUT,
    MetalTexture*  pIrradianceTexture,
    MetalTexture*  pEnvironmentTexture,
    uint32_t*      pEnvNumLevels);

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
        std::string shaderSource = LoadString("projects/201_202_pbr_spheres/shaders.metal");
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
        std::string shaderSource = LoadString("projects/201_202_pbr_spheres/drawtexture.metal");
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
    CHECK_CALL(CreateDrawNormalPipeline(
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
    // Material sphere vertex buffers
    // *************************************************************************
    uint32_t    materialSphereNumIndices = 0;
    MetalBuffer materialSphereIndexBuffer;
    MetalBuffer materialSpherePositionBuffer;
    MetalBuffer materialSphereNormalBuffer;
    CreateMaterialSphereVertexBuffers(
        renderer.get(),
        &materialSphereNumIndices,
        &materialSphereIndexBuffer,
        &materialSpherePositionBuffer,
        &materialSphereNormalBuffer);

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
    // IBL texture
    // *************************************************************************
    MetalTexture brdfLUT;
    MetalTexture irrTexture;
    MetalTexture envTexture;
    uint32_t     envNumLevels = 0;
    CreateIBLTextures(renderer.get(), &brdfLUT, &irrTexture, &envTexture, &envNumLevels);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "201_pbr_spheres_metal");
    if (!window) {
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
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT)) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get())) {
        assert(false && "Window::InitImGuiForMetal failed");
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

        CA::MetalDrawable* pDrawable = renderer->Swapchain->nextDrawable();

        // nextDrawable() will return nil if there are no free swapchain buffers to render to
        if (pDrawable) {
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
            vec3 eyePosition = vec3(0, 0, 9);
            mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
            mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));

            // Set constant buffer values
            SceneParameters sceneParams         = {};
            sceneParams.viewProjectionMatrix    = projMat * viewMat;
            sceneParams.eyePosition             = eyePosition;
            sceneParams.numLights               = gNumLights;
            sceneParams.lights[0].position      = vec3(5, 7, 32);
            sceneParams.lights[0].color         = vec3(0.98f, 0.85f, 0.71f);
            sceneParams.lights[0].intensity     = 0.5f;
            sceneParams.lights[1].position      = vec3(-8, 1, 4);
            sceneParams.lights[1].color         = vec3(1.00f, 0.00f, 0.00f);
            sceneParams.lights[1].intensity     = 0.5f;
            sceneParams.lights[2].position      = vec3(0, 8, -8);
            sceneParams.lights[2].color         = vec3(0.00f, 1.00f, 0.00f);
            sceneParams.lights[2].intensity     = 0.5f;
            sceneParams.lights[3].position      = vec3(15, 8, 0);
            sceneParams.lights[3].color         = vec3(0.00f, 0.00f, 1.00f);
            sceneParams.lights[3].intensity     = 0.5f;
            sceneParams.iblEnvironmentNumLevels = envNumLevels;

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

            // Draw material sphere
            {
                // SceneParams [[buffer(3)]]
                pRenderEncoder->setVertexBytes(&sceneParams, sizeof(SceneParameters), 3);
                pRenderEncoder->setFragmentBytes(&sceneParams, sizeof(SceneParameters), 3);
                // IBL textures [[texture(0,1,2)]]
                pRenderEncoder->setFragmentTexture(brdfLUT.Texture.get(), 0);
                pRenderEncoder->setFragmentTexture(irrTexture.Texture.get(), 1);
                pRenderEncoder->setFragmentTexture(envTexture.Texture.get(), 2);

                // Vertex buffers
                MTL::Buffer* vbvs[2]    = {};
                NS::UInteger offsets[2] = {};
                NS::Range    vbRange(0, 2);
                // Position
                vbvs[0]    = materialSpherePositionBuffer.Buffer.get();
                offsets[0] = 0;
                // Tex coord
                vbvs[1]    = materialSphereNormalBuffer.Buffer.get();
                offsets[1] = 0;
                pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

                pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
                pRenderEncoder->setCullMode(MTL::CullModeFront);

                // Pipeline state
                pRenderEncoder->setRenderPipelineState(pbrPipelineState.State.get());
                pRenderEncoder->setDepthStencilState(pbrDepthStencilState.State.get());

                pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
                pRenderEncoder->setCullMode(MTL::CullModeBack);

                MaterialParameters materialParams = {};
                materialParams.baseColor          = vec3(0.8f, 0.8f, 0.9f);
                materialParams.roughness          = 0;
                materialParams.metallic           = 0;

                uint32_t numSlotsX     = 10;
                uint32_t numSlotsY     = 10;
                float    slotSize      = 0.9f;
                float    spanX         = numSlotsX * slotSize;
                float    spanY         = numSlotsY * slotSize;
                float    halfSpanX     = spanX / 2.0f;
                float    halfSpanY     = spanY / 2.0f;
                float    roughnessStep = 1.0f / (numSlotsX - 1);
                float    metalnessStep = 1.0f / (numSlotsY - 1);

                for (uint32_t i = 0; i < numSlotsY; ++i) {
                    materialParams.metallic = 0;

                    for (uint32_t j = 0; j < numSlotsX; ++j) {
                        float x = -halfSpanX + j * slotSize;
                        float y = -halfSpanY + i * slotSize;
                        float z = 0;
                        // Readjust center
                        x += slotSize / 2.0f;
                        y += slotSize / 2.0f;

                        glm::mat4 modelMat = rotMat * glm::translate(vec3(x, y, z));
                        // DrawParams [[buffer(2)]]
                        pRenderEncoder->setVertexBytes(&modelMat, sizeof(glm::mat4), 2);
                        pRenderEncoder->setFragmentBytes(&modelMat, sizeof(glm::mat4), 2);
                        // MaterialParams [[buffer(4)]]
                        pRenderEncoder->setFragmentBytes(&materialParams, sizeof(MaterialParameters), 4);

                        pRenderEncoder->drawIndexedPrimitives(
                            MTL::PrimitiveType::PrimitiveTypeTriangle,
                            materialSphereNumIndices,
                            MTL::IndexTypeUInt32,
                            materialSphereIndexBuffer.Buffer.get(),
                            0);

                        materialParams.metallic += metalnessStep;
                    }
                    materialParams.roughness += metalnessStep;
                }
            }

            // Draw ImGui
            window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

            pRenderEncoder->endEncoding();

            pCommandBuffer->presentDrawable(pDrawable);
            pCommandBuffer->commit();
        }
    }

    return 0;
}

void CreateMaterialSphereVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pNormalBuffer)
{
    TriMesh::Options options;
    options.enableNormals = true;

    TriMesh mesh = TriMesh::Sphere(0.42f, 256, 256, options);

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
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        pNormalBuffer));
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
    auto iblFile = GetAssetPath("IBL/old_depot_4k.ibl");

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
