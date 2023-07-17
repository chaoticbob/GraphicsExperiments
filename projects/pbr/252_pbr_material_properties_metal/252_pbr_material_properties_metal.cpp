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

#define ROW_METALLIC               0
#define ROW_ROUGHNESS_NON_METALLIC 1
#define ROW_ROUGHNESS_METALLIC     2
#define ROW_REFLECTANCE            3
#define ROW_CLEAR_COAT             4
#define ROW_CLEAR_COAT_ROUGHNESS   5
#define ROW_ANISOTROPY             6

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
    uint     multiscatter;
    uint     furnace;
    uint32_t __pad2;
};

struct MaterialParameters
{
    vec3     baseColor;
    uint32_t __pad0;
    float    roughness;
    float    metallic;
    float    reflectance;
    float    clearCoat;
    float    clearCoatRoughness;
    float    anisotropy;
    uint32_t __pad1[2];
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 3470;
static uint32_t gWindowHeight = 1920;
static bool     gEnableDebug  = true;

static uint32_t gGridStartX       = 485;
static uint32_t gGridStartY       = 15;
static uint32_t gGridTextHeight   = 28;
static uint32_t gCellStrideX      = 270;
static uint32_t gCellStrideY      = 270;
static uint32_t gCellResX         = gCellStrideX;
static uint32_t gCellResY         = gCellStrideY - gGridTextHeight;
static uint32_t gCellRenderResX   = gCellResX - 10;
static uint32_t gCellRenderResY   = gCellResY - 10;
static uint32_t gCellRenderStartX = gGridStartX + (gCellResX - gCellRenderResX) / 2;
static uint32_t gCellRenderStartY = gGridStartY + gGridTextHeight + (gCellResY - gCellRenderResY) / 2;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static uint32_t gNumLights = 0;

void CreateMaterialSphereVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pNormalBuffer,
    MetalBuffer*   pTangentBuffer,
    MetalBuffer*   pBitangentBuffer);
void CreateEnvironmentVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pTexCoordBuffer);
void CreateIBLTextures(
    MetalRenderer* pRenderer,
    MetalTexture*  pBRDFLUT,
    MetalTexture*  pMultiscatterBRDFLUT,
    MetalTexture*  pIrradianceTexture,
    MetalTexture*  pEnvironmentTexture,
    uint32_t*      pEnvNumLevels,
    MetalTexture*  pFurnaceTexture);

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
        std::string shaderSource = LoadString("projects/252_pbr_material_properties/shaders.metal");
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
        std::string shaderSource = LoadString("projects/252_pbr_material_properties/drawtexture.metal");
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
        &pbrDepthStencilState,
        true /* enableTangents */));

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
    MetalBuffer materialSphereTangentBuffer;
    MetalBuffer materialSphereBitangentBuffer;
    CreateMaterialSphereVertexBuffers(
        renderer.get(),
        &materialSphereNumIndices,
        &materialSphereIndexBuffer,
        &materialSpherePositionBuffer,
        &materialSphereNormalBuffer,
        &materialSphereTangentBuffer,
        &materialSphereBitangentBuffer);

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
    MetalTexture multiscatterBRDFLUT;
    MetalTexture irrTexture;
    MetalTexture envTexture;
    MetalTexture furnaceTexture;
    uint32_t     envNumLevels = 0;
    CreateIBLTextures(
        renderer.get(),
        &brdfLUT,
        &multiscatterBRDFLUT,
        &irrTexture,
        &envTexture,
        &envNumLevels,
        &furnaceTexture);

    // *************************************************************************
    // Material template
    // *************************************************************************
    MetalTexture materialTemplateTexture;
    {
        auto bitmap = LoadImage8u(GetAssetPath("textures/material_properties_template.png"));
        CHECK_CALL(CreateTexture(
            renderer.get(),
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            MTL::PixelFormatRGBA8Unorm,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            &materialTemplateTexture));
    }

    MetalTexture whiteTexture;
    {
        BitmapRGBA8u whiteBitmap(gCellRenderResX, gCellRenderResY);
        PixelRGBA8u  whitePixel = {255, 255, 255, 255};
        whiteBitmap.Fill(whitePixel);

        CHECK_CALL(CreateTexture(
            renderer.get(),
            whiteBitmap.GetWidth(),
            whiteBitmap.GetHeight(),
            MTL::PixelFormatRGBA8Unorm,
            whiteBitmap.GetSizeInBytes(),
            whiteBitmap.GetPixels(),
            &whiteTexture));
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "252_pbr_material_properties_metal");
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

    // Disable framebufferOnly so we can copy the background texture to the framebuffer
    renderer->pSwapchain->setFramebufferOnly(false);

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get())) {
        assert(false && "Window::InitImGuiForMetal failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Persistent map scene parameters
    // *************************************************************************
    SceneParameters sceneParams = {};

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents()) {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

        if (ImGui::Begin("Scene")) {
            ImGui::Checkbox("Mutilscatter", reinterpret_cast<bool*>(&sceneParams.multiscatter));
            ImGui::Checkbox("Furnace", reinterpret_cast<bool*>(&sceneParams.furnace));
        }
        ImGui::End();

        MTL::Texture* descriptorTexture;
        if (sceneParams.furnace) {
            descriptorTexture                   = furnaceTexture.Texture.get();
            sceneParams.iblEnvironmentNumLevels = 1;
        }
        else {
            descriptorTexture                   = envTexture.Texture.get();
            sceneParams.iblEnvironmentNumLevels = envNumLevels;
        }

        // ---------------------------------------------------------------------

        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

        uint32_t swapchainIndex = (frameIndex % renderer->SwapchainBufferCount);
        frameIndex++;

        auto colorTargetDesc = NS::TransferPtr(MTL::RenderPassColorAttachmentDescriptor::alloc()->init());
        colorTargetDesc->setClearColor(clearColor);
        colorTargetDesc->setTexture(pDrawable->texture());
        colorTargetDesc->setLoadAction(MTL::LoadActionLoad);
        colorTargetDesc->setStoreAction(MTL::StoreActionStore);
        pRenderPassDescriptor->colorAttachments()->setObject(colorTargetDesc.get(), 0);

        auto depthTargetDesc = NS::TransferPtr(MTL::RenderPassDepthAttachmentDescriptor::alloc()->init());
        depthTargetDesc->setClearDepth(1);
        depthTargetDesc->setTexture(renderer->SwapchainDSVBuffers[swapchainIndex].get());
        depthTargetDesc->setLoadAction(MTL::LoadActionClear);
        depthTargetDesc->setStoreAction(MTL::StoreActionDontCare);
        pRenderPassDescriptor->setDepthAttachment(depthTargetDesc.get());

        MTL::CommandBuffer* pCommandBuffer = renderer->Queue->commandBuffer();

        MTL::BlitCommandEncoder* pBlitEncoder = pCommandBuffer->blitCommandEncoder();
        pBlitEncoder->copyFromTexture(materialTemplateTexture.Texture.get(), pDrawable->texture());

        // If furnace is enabled, clear out all the render targets by drawing the white texture to each viewport location
        if (sceneParams.furnace) {
            uint32_t cellY = gCellRenderStartY;
            for (uint32_t yi = 0; yi < 7; ++yi) {
                uint32_t cellX = gCellRenderStartX;
                for (uint32_t xi = 0; xi < 11; ++xi) {
                    MTL::Origin sourceOrigin(0, 0, 0);
                    MTL::Size   sourceSize(gCellRenderResX, gCellRenderResY, 1);
                    MTL::Origin destOrigin(cellX, cellY, 0);

                    pBlitEncoder->copyFromTexture(whiteTexture.Texture.get(), 0, 0, sourceOrigin, sourceSize, pDrawable->texture(), 0, 0, destOrigin);

                    // ---------------------------------------------------------
                    // Next cell
                    // ---------------------------------------------------------
                    cellX += gCellStrideX;
                }
                cellY += gCellStrideY;
            }
        }

        pBlitEncoder->endEncoding();

        MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);

        MTL::Viewport viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
        pRenderEncoder->setViewport(viewport);
        MTL::ScissorRect scissor = {0, 0, gWindowWidth, gWindowHeight};
        pRenderEncoder->setScissorRect(scissor);

        // -----------------------------------------------------------------
        // Scene variables
        // -----------------------------------------------------------------
        // Camera matrices
        vec3 eyePosition = vec3(0, 0, 0.85f);
        mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat     = glm::perspective(glm::radians(60.0f), gCellRenderResX / static_cast<float>(gCellRenderResY), 0.1f, 10000.0f);
        mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));

        // Set constant buffer values
        sceneParams.viewProjectionMatrix    = projMat * viewMat;
        sceneParams.eyePosition             = eyePosition;
        sceneParams.numLights               = 1;
        sceneParams.lights[0].position      = vec3(-5, 5, 3);
        sceneParams.lights[0].color         = vec3(1, 1, 1);
        sceneParams.lights[0].intensity     = 1.5f;
        sceneParams.iblEnvironmentNumLevels = envNumLevels;

        // -----------------------------------------------------------------
        // Descriptors
        // -----------------------------------------------------------------
        // SceneParams [[buffer(4)]]
        pRenderEncoder->setVertexBytes(&sceneParams, sizeof(SceneParameters), 4);
        pRenderEncoder->setFragmentBytes(&sceneParams, sizeof(SceneParameters), 4);
        // IBL textures [[texture(0,1,2,3)]]
        pRenderEncoder->setFragmentTexture(brdfLUT.Texture.get(), 0);
        pRenderEncoder->setFragmentTexture(multiscatterBRDFLUT.Texture.get(), 1);
        pRenderEncoder->setFragmentTexture(irrTexture.Texture.get(), 2);
        pRenderEncoder->setFragmentTexture(descriptorTexture, 3);

        // -----------------------------------------------------------------
        // Pipeline state
        // -----------------------------------------------------------------
        pRenderEncoder->setRenderPipelineState(pbrPipelineState.State.get());
        pRenderEncoder->setDepthStencilState(pbrDepthStencilState.State.get());

        // Vertex buffers
        MTL::Buffer* vbvs[4]    = {};
        NS::UInteger offsets[4] = {};
        NS::Range    vbRange(0, 4);
        // Position
        vbvs[0]    = materialSpherePositionBuffer.Buffer.get();
        offsets[0] = 0;
        // Normals
        vbvs[1]    = materialSphereNormalBuffer.Buffer.get();
        offsets[1] = 0;
        // Tangents
        vbvs[2]    = materialSphereTangentBuffer.Buffer.get();
        offsets[2] = 0;
        // Bitangents
        vbvs[3]    = materialSphereBitangentBuffer.Buffer.get();
        offsets[3] = 0;
        pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

        pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
        pRenderEncoder->setCullMode(MTL::CullModeBack);

        // -----------------------------------------------------------------
        // Draw material spheres
        // -----------------------------------------------------------------
        const float clearColor[4] = {1, 1, 1, 1};
        uint32_t    cellY         = gCellRenderStartY;
        float       dt            = 1.0f / 10;
        for (uint32_t yi = 0; yi < 7; ++yi) {
            uint32_t cellX = gCellRenderStartX;
            float    t     = 0;
            for (uint32_t xi = 0; xi < 11; ++xi) {
                MTL::ScissorRect cellRect = {};
                cellRect.x                = cellX;
                cellRect.y                = cellY;
                cellRect.width            = gCellRenderResX;
                cellRect.height           = gCellRenderResY;

                // ---------------------------------------------------------
                // Set viewport and scissor
                // ---------------------------------------------------------
                MTL::Viewport viewport = {
                    static_cast<float>(cellX),
                    static_cast<float>(cellY),
                    static_cast<float>(gCellRenderResX),
                    static_cast<float>(gCellRenderResY),
                    0,
                    1};
                pRenderEncoder->setViewport(viewport);
                pRenderEncoder->setScissorRect(cellRect);

                // ---------------------------------------------------------
                // Draw material sphere
                // ---------------------------------------------------------
                MaterialParameters materialParams = {};
                materialParams.baseColor          = vec3(1.0f, 1.0f, 1.0f);
                materialParams.roughness          = 0;
                materialParams.metallic           = 0;
                materialParams.reflectance        = 0.5;
                materialParams.clearCoat          = 0;
                materialParams.clearCoatRoughness = 0;

                switch (yi) {
                    default: break;
                    case ROW_METALLIC: {
                        materialParams.baseColor = F0_MetalChromium;
                        materialParams.metallic  = t;
                        materialParams.roughness = 0;
                    } break;

                    case ROW_ROUGHNESS_NON_METALLIC: {
                        materialParams.baseColor = vec3(0, 0, 0.75f);
                        materialParams.roughness = std::max(0.045f, t);
                    } break;

                    case ROW_ROUGHNESS_METALLIC: {
                        materialParams.baseColor = sceneParams.furnace ? vec3(1) : F0_MetalGold;
                        materialParams.roughness = std::max(0.045f, t);
                        materialParams.metallic  = 1.0;
                    } break;

                    case ROW_REFLECTANCE: {
                        materialParams.baseColor   = vec3(0.75f, 0, 0);
                        materialParams.roughness   = 0.2f;
                        materialParams.metallic    = 0;
                        materialParams.reflectance = t;
                    } break;

                    case ROW_CLEAR_COAT: {
                        materialParams.baseColor = vec3(0.75f, 0, 0);
                        materialParams.roughness = 0.8f;
                        materialParams.metallic  = 1.0f;
                        materialParams.clearCoat = t;
                    } break;

                    case ROW_CLEAR_COAT_ROUGHNESS: {
                        materialParams.baseColor          = vec3(0.75f, 0, 0);
                        materialParams.roughness          = 0.8f;
                        materialParams.metallic           = 1.0f;
                        materialParams.clearCoat          = 1;
                        materialParams.clearCoatRoughness = std::max(0.045f, t);
                    } break;

                    case ROW_ANISOTROPY: {
                        materialParams.baseColor  = F0_MetalZinc;
                        materialParams.roughness  = 0.45f;
                        materialParams.metallic   = 1.0f;
                        materialParams.anisotropy = t;
                    } break;
                }

                glm::mat4 modelMat = glm::mat4(1);
                // DrawParams [[buffer(5,6)]]
                pRenderEncoder->setVertexBytes(&modelMat, sizeof(glm::mat4), 5);
                pRenderEncoder->setFragmentBytes(&modelMat, sizeof(glm::mat4), 6);
                // MaterialParams [[buffer(5)]]
                pRenderEncoder->setFragmentBytes(&materialParams, sizeof(MaterialParameters), 5);

                // Draw
                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    materialSphereNumIndices,
                    MTL::IndexTypeUInt32,
                    materialSphereIndexBuffer.Buffer.get(),
                    0);

                // ---------------------------------------------------------
                // Next cell
                // ---------------------------------------------------------
                cellX += gCellStrideX;
                t += dt;
            }
            cellY += gCellStrideY;
        }
        // Draw ImGui
        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateMaterialSphereVertexBuffers(
    MetalRenderer* pRenderer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pNormalBuffer,
    MetalBuffer*   pTangentBuffer,
    MetalBuffer*   pBitangentBuffer)
{
    TriMesh::Options options;
    options.enableNormals  = true;
    options.enableTangents = true;

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

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTangents()),
        DataPtr(mesh.GetTangents()),
        pTangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetBitangents()),
        DataPtr(mesh.GetBitangents()),
        pBitangentBuffer));
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
    MetalTexture*  pMultiscatterBRDFLUT,
    MetalTexture*  pIrradianceTexture,
    MetalTexture*  pEnvironmentTexture,
    uint32_t*      pEnvNumLevels,
    MetalTexture*  pFurnaceTexture)
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

    // Multiscatter BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut_ms.hdr"));
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
            pMultiscatterBRDFLUT));
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

    // Furnace
    {
        BitmapRGBA32f bitmap(32, 16);
        bitmap.Fill(PixelRGBA32f{1, 1, 1, 1});

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            MTL::PixelFormatRGBA32Float,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pFurnaceTexture));
    }
}
