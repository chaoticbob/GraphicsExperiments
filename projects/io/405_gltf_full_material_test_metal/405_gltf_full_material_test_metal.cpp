#include "window.h"

#include "mtl_renderer.h"

#include "mtl_faux_render.h"

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

#define MAX_INSTANCES         100
#define MAX_MATERIALS         100
#define MAX_MATERIAL_SAMPLERS 32
#define MAX_MATERIAL_IMAGES   1024
#define MAX_IBL_TEXTURES      1

#define SCENE_REGISTER                     4
#define CAMERA_REGISTER                    5
#define DRAW_REGISTER                      6
#define INSTANCE_BUFFER_REGISTER           7
#define MATERIAL_BUFFER_REGISTER           8
#define MATERIAL_SAMPLER_START_REGISTER    9
#define MATERIAL_IMAGES_START_REGISTER     10
#define IBL_ENV_MAP_TEXTURE_START_REGISTER 11
#define IBL_IRR_MAP_TEXTURE_START_REGISTER 12
#define IBL_INTEGRATION_LUT_REGISTER       13
#define IBL_MAP_SAMPLER_REGISTER           14
#define IBL_INTEGRATION_SAMPLER_REGISTER   15

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth      = 1280;
static uint32_t gWindowHeight     = 720;
static bool     gEnableDebug      = true;

static const uint32_t gNumIBLLUTs            = 2;
static const uint32_t gNumIBLTextures        = 1;
static const uint32_t gNumIBLEnvTextures     = gNumIBLTextures;
static const uint32_t gNumIBLIrrTextures     = gNumIBLTextures;
static const uint32_t gIBLLUTsOffset         = 0;
static const uint32_t gIBLEnvTextureOffset   = gIBLLUTsOffset + gNumIBLLUTs;
static const uint32_t gIBLIrrTextureOffset   = gIBLEnvTextureOffset + gNumIBLEnvTextures;
static const uint32_t gMaterialTextureOffset = gIBLIrrTextureOffset + gNumIBLIrrTextures;

static std::vector<std::string> gIBLNames = {};

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

void CreateIBLTextures(
    MetalRenderer*             pRenderer,
    MetalTexture*              ppBRDFLUT,
    MetalTexture*              ppMultiscatterBRDFLUT,
    std::vector<MetalTexture>& outIrradianceTextures,
    std::vector<MetalTexture>& outEnvironmentTextures,
    std::vector<uint32_t>&     outEnvNumLevels);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT)
    {
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

    if (!InitMetal(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::string shaderSource = LoadString("faux_render_shaders/render_pbr_material.metal");

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
    // Scene
    // *************************************************************************
    MtlFauxRender::SceneGraph graph = MtlFauxRender::SceneGraph(renderer.get());
    if (!FauxRender::LoadGLTF(GetAssetPath("scenes/material_test_001_ktx2/material_test_001.gltf"), {}, &graph))
    {
        assert(false && "LoadGLTF failed");
        return EXIT_FAILURE;
    }
    if (!graph.InitializeResources())
    {
        assert(false && "Graph resources initialization failed");
        return EXIT_FAILURE;
    }

    graph.RootParameterIndices.Scene          = SCENE_REGISTER;
    graph.RootParameterIndices.Camera         = CAMERA_REGISTER;
    graph.RootParameterIndices.Draw           = DRAW_REGISTER;
    graph.RootParameterIndices.InstanceBuffer = INSTANCE_BUFFER_REGISTER;
    graph.RootParameterIndices.MaterialBuffer = MATERIAL_BUFFER_REGISTER;

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    MetalPipelineRenderState renderPipelineState;
    MetalDepthStencilState   depthStencilState;
    CHECK_CALL(CreateGraphicsPipeline2(
        renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &renderPipelineState,
        &depthStencilState));


    // *************************************************************************
    // IBL textures
    // *************************************************************************
    MetalTexture              brdfLUT;
    MetalTexture              multiscatterBRDFLUT;
    std::vector<MetalTexture> irrTextures;
    std::vector<MetalTexture> envTextures;
    std::vector<uint32_t>     envNumLevels;
    CreateIBLTextures(
        renderer.get(),
        &brdfLUT,
        &multiscatterBRDFLUT,
        irrTextures,
        envTextures,
        envNumLevels);

    // *************************************************************************
    // ArgBuffers
    // *************************************************************************
    MTL::Buffer*                    pMaterialImagesArgBuffer   = nullptr;
    MTL::Buffer*                    pMaterialSamplersArgBuffer = nullptr;
    MTL::Buffer*                    pIrrImagesArgBuffer = nullptr;
    MTL::Buffer*                    pEnvImagesArgBuffer = nullptr;
    MTL::SamplerState*              pIBLMapSamplerState        = nullptr;
    MTL::SamplerState*              pIBLIntegrationSamplerState     = nullptr;
    std::vector<MTL::Texture*>      materialImagesTextures;
    std::vector<MTL::SamplerState*> materialSamplerStates;
    std::vector<MTL::Texture*>      irrImagesTextures;
    std::vector<MTL::Texture*>      envImagesTextures;
    {
        // Material Textures
        {
            MTL::ArgumentEncoder* pMaterialImagesArgEncoder = fsShader.Function->newArgumentEncoder(MATERIAL_IMAGES_START_REGISTER);
            pMaterialImagesArgBuffer                        = renderer->Device->newBuffer(
                pMaterialImagesArgEncoder->encodedLength(),
                MTL::ResourceStorageModeManaged);

            pMaterialImagesArgEncoder->setArgumentBuffer(pMaterialImagesArgBuffer, 0);

            for (size_t i = 0; i < graph.Images.size(); ++i)
            {
                auto          image    = MtlFauxRender::Cast(graph.Images[i].get());
                MTL::Texture* resource = image->Resource.Texture.get();

                pMaterialImagesArgEncoder->setTexture(resource, i);
                materialImagesTextures.push_back(resource);
            }

            pMaterialImagesArgBuffer->didModifyRange(NS::Range::Make(0, graph.Images.size()));

            pMaterialImagesArgEncoder->release();
        }

        // Material Samplers
        {
            uint32_t              samplerCount                = 0;
            MTL::ArgumentEncoder* pMaterialSamplersArgEncoder = fsShader.Function->newArgumentEncoder(MATERIAL_SAMPLER_START_REGISTER);
            pMaterialSamplersArgBuffer                        = renderer->Device->newBuffer(
                pMaterialSamplersArgEncoder->encodedLength(),
                MTL::ResourceStorageModeManaged);

            pMaterialSamplersArgEncoder->setArgumentBuffer(pMaterialSamplersArgBuffer, 0);

            // Clamped
            {
                MTL::SamplerDescriptor* clampedSamplerDesc = MTL::SamplerDescriptor::alloc()->init();

                if (clampedSamplerDesc != nullptr)
                {
                    clampedSamplerDesc->setSupportArgumentBuffers(true);
                    clampedSamplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
                    clampedSamplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
                    clampedSamplerDesc->setMipFilter(MTL::SamplerMipFilterLinear);
                    clampedSamplerDesc->setRAddressMode(MTL::SamplerAddressModeClampToEdge);
                    clampedSamplerDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
                    clampedSamplerDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
                    clampedSamplerDesc->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);

                    MTL::SamplerState* clampedSampler = renderer->Device->newSamplerState(clampedSamplerDesc);
                    pMaterialSamplersArgEncoder->setSamplerState(clampedSampler, samplerCount);
                    samplerCount++;

                    clampedSamplerDesc->release();
                }
                else
                {
                    assert(false && "Clamped sampler creation failure");
                    return EXIT_FAILURE;
                }
            }

            // Repeat
            {
                MTL::SamplerDescriptor* repeatSamplerDesc = MTL::SamplerDescriptor::alloc()->init();

                if (repeatSamplerDesc != nullptr)
                {
                    repeatSamplerDesc->setSupportArgumentBuffers(true);
                    repeatSamplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
                    repeatSamplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
                    repeatSamplerDesc->setMipFilter(MTL::SamplerMipFilterLinear);
                    repeatSamplerDesc->setRAddressMode(MTL::SamplerAddressModeRepeat);
                    repeatSamplerDesc->setSAddressMode(MTL::SamplerAddressModeRepeat);
                    repeatSamplerDesc->setTAddressMode(MTL::SamplerAddressModeRepeat);
                    repeatSamplerDesc->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);

                    MTL::SamplerState* repeatSampler = renderer->Device->newSamplerState(repeatSamplerDesc);
                    pMaterialSamplersArgEncoder->setSamplerState(repeatSampler, samplerCount);
                    samplerCount++;

                    repeatSamplerDesc->release();
                }
                else
                {
                    assert(false && "Repeat sampler creation failure");
                    return EXIT_FAILURE;
                }

                pMaterialSamplersArgBuffer->didModifyRange(NS::Range::Make(0, samplerCount));
            }

            pMaterialSamplersArgEncoder->release();
        }

        // IBL Textures
        {
			// Irradiance Textures
            {
                MTL::ArgumentEncoder* pIrrImagesArgEncoder =
                    fsShader.Function->newArgumentEncoder(IBL_IRR_MAP_TEXTURE_START_REGISTER);

                pIrrImagesArgBuffer = renderer->Device->newBuffer(
                    pIrrImagesArgEncoder->encodedLength(),
                    MTL::ResourceStorageModeManaged);

                pIrrImagesArgEncoder->setArgumentBuffer(pIrrImagesArgBuffer, 0);

                for (size_t i = 0; i < irrTextures.size(); ++i)
                {
                    MTL::Texture* irrTexture = irrTextures[i].Texture.get();
                    pIrrImagesArgEncoder->setTexture(irrTexture, i);
                    irrImagesTextures.push_back(irrTexture);
                }

                pIrrImagesArgBuffer->didModifyRange(NS::Range::Make(0, irrImagesTextures.size()));
                pIrrImagesArgEncoder->release();
            }

			// Environment textures
            {
                MTL::ArgumentEncoder* pEnvImagesArgEncoder =
                    fsShader.Function->newArgumentEncoder(IBL_IRR_MAP_TEXTURE_START_REGISTER);

                pEnvImagesArgBuffer = renderer->Device->newBuffer(
                    pEnvImagesArgEncoder->encodedLength(),
                    MTL::ResourceStorageModeManaged);

                pEnvImagesArgEncoder->setArgumentBuffer(pEnvImagesArgBuffer, 0);

                for (size_t i = 0; i < envTextures.size(); ++i)
                {
                    MTL::Texture* envTexture = envTextures[i].Texture.get();
                    pEnvImagesArgEncoder->setTexture(envTexture, i);
                    envImagesTextures.push_back(envTexture);
                }

                pEnvImagesArgBuffer->didModifyRange(NS::Range::Make(0, envImagesTextures.size()));
                pEnvImagesArgEncoder->release();
            }
        }

		// IBL Samplers
        {
            uint32_t samplerCount = 0;

            // IBL Map Sampler
            {
                MTL::SamplerDescriptor* iblMapSamplerDesc = MTL::SamplerDescriptor::alloc()->init();

                if (iblMapSamplerDesc != nullptr)
                {
                    iblMapSamplerDesc->setSupportArgumentBuffers(true);
                    iblMapSamplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
                    iblMapSamplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
                    iblMapSamplerDesc->setMipFilter(MTL::SamplerMipFilterLinear);
                    iblMapSamplerDesc->setRAddressMode(MTL::SamplerAddressModeClampToEdge);
                    iblMapSamplerDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
                    iblMapSamplerDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
                    iblMapSamplerDesc->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);

                    pIBLMapSamplerState = renderer->Device->newSamplerState(iblMapSamplerDesc);
                    samplerCount++;

                    iblMapSamplerDesc->release();
                }
                else
                {
                    assert(false && "IBL Map sampler creation failure");
                    return EXIT_FAILURE;
                }
            }

            // Repeat
            {
                MTL::SamplerDescriptor* iblIntegrationSamplerDesc = MTL::SamplerDescriptor::alloc()->init();

                if (iblIntegrationSamplerDesc != nullptr)
                {
                    iblIntegrationSamplerDesc->setSupportArgumentBuffers(true);
                    iblIntegrationSamplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
                    iblIntegrationSamplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
                    iblIntegrationSamplerDesc->setMipFilter(MTL::SamplerMipFilterLinear);
                    iblIntegrationSamplerDesc->setRAddressMode(MTL::SamplerAddressModeRepeat);
                    iblIntegrationSamplerDesc->setSAddressMode(MTL::SamplerAddressModeRepeat);
                    iblIntegrationSamplerDesc->setTAddressMode(MTL::SamplerAddressModeRepeat);
                    iblIntegrationSamplerDesc->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);

                    pIBLIntegrationSamplerState = renderer->Device->newSamplerState(iblIntegrationSamplerDesc);
                    samplerCount++;

                    iblIntegrationSamplerDesc->release();
                }
                else
                {
                    assert(false && "Repeat sampler creation failure");
                    return EXIT_FAILURE;
                }
            }
        }
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "405_gltf_full_material_test_case_metal");
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
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents())
    {
        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

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

        MTL::Viewport viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
        pRenderEncoder->setViewport(viewport);

        MTL::ScissorRect scissor = {0, 0, gWindowWidth, gWindowHeight};
        pRenderEncoder->setScissorRect(scissor);

        pRenderEncoder->setRenderPipelineState(renderPipelineState.State.get());
        pRenderEncoder->setDepthStencilState(depthStencilState.State.get());

        pRenderEncoder->setVertexBytes(&envNumLevels[0], 16, SCENE_REGISTER);
        pRenderEncoder->setFragmentBytes(&envNumLevels[0], 16, SCENE_REGISTER);

        for (size_t i = 0; i < materialImagesTextures.size(); ++i)
        {
           pRenderEncoder->useResource(materialImagesTextures[i], MTL::ResourceUsageRead);
        }

        pRenderEncoder->setVertexBuffer(pMaterialImagesArgBuffer, 0, MATERIAL_IMAGES_START_REGISTER);
        pRenderEncoder->setFragmentBuffer(pMaterialImagesArgBuffer, 0, MATERIAL_IMAGES_START_REGISTER);

        pRenderEncoder->setVertexBuffer(pMaterialSamplersArgBuffer, 0, MATERIAL_SAMPLER_START_REGISTER);
        pRenderEncoder->setFragmentBuffer(pMaterialSamplersArgBuffer, 0, MATERIAL_SAMPLER_START_REGISTER);

        pRenderEncoder->setVertexTexture(brdfLUT.Texture.get(), IBL_INTEGRATION_LUT_REGISTER);
        pRenderEncoder->setFragmentTexture(brdfLUT.Texture.get(), IBL_INTEGRATION_LUT_REGISTER);

        for (size_t i = 0; i < irrImagesTextures.size(); ++i)
        {
           pRenderEncoder->useResource(irrImagesTextures[i], MTL::ResourceUsageRead);
        }

        pRenderEncoder->setVertexBuffer(pIrrImagesArgBuffer, 0, IBL_IRR_MAP_TEXTURE_START_REGISTER);
        pRenderEncoder->setFragmentBuffer(pIrrImagesArgBuffer, 0, IBL_IRR_MAP_TEXTURE_START_REGISTER);

        for (size_t i = 0; i < envImagesTextures.size(); ++i)
        {
           pRenderEncoder->useResource(envImagesTextures[i], MTL::ResourceUsageRead);
        }

        pRenderEncoder->setVertexBuffer(pEnvImagesArgBuffer, 0, IBL_ENV_MAP_TEXTURE_START_REGISTER);
        pRenderEncoder->setFragmentBuffer(pEnvImagesArgBuffer, 0, IBL_ENV_MAP_TEXTURE_START_REGISTER);

        pRenderEncoder->setVertexSamplerState(pIBLMapSamplerState, IBL_MAP_SAMPLER_REGISTER);
        pRenderEncoder->setFragmentSamplerState(pIBLMapSamplerState, IBL_MAP_SAMPLER_REGISTER);

        pRenderEncoder->setVertexSamplerState(pIBLIntegrationSamplerState, IBL_INTEGRATION_SAMPLER_REGISTER);
        pRenderEncoder->setFragmentSamplerState(pIBLIntegrationSamplerState, IBL_INTEGRATION_SAMPLER_REGISTER);

        // Draw scene
        const auto& scene = graph.Scenes[0];
        MtlFauxRender::Draw(&graph, scene.get(), pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateIBLTextures(
    MetalRenderer*             pRenderer,
    MetalTexture*              ppBRDFLUT,
    MetalTexture*              ppMultiscatterBRDFLUT,
    std::vector<MetalTexture>& outIrradianceTextures,
    std::vector<MetalTexture>& outEnvironmentTextures,
    std::vector<uint32_t>&     outEnvNumLevels)
{
    // BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
        if (bitmap.Empty())
        {
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
            ppBRDFLUT));
    }

    // Multiscatter BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut_ms.hdr"));
        if (bitmap.Empty())
        {
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
            ppMultiscatterBRDFLUT));
    }

    // auto                               iblDir = GetAssetPath("IBL");
    // std::vector<std::filesystem::path> iblFiles;
    // for (auto& entry : std::filesystem::directory_iterator(iblDir))
    //{
    //     if (!entry.is_regular_file())
    //     {
    //         continue;
    //     }
    //     auto path = entry.path();
    //     auto ext  = path.extension();
    //     if (ext == ".ibl")
    //     {
    //         path = std::filesystem::relative(path, iblDir.parent_path());
    //         iblFiles.push_back(path);
    //     }
    // }

    std::vector<std::filesystem::path> iblFiles = {GetAssetPath("IBL/machine_shop_01_4k.ibl")};

    size_t maxEntries = std::min<size_t>(gNumIBLTextures, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i)
    {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl))
        {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            assert(false && "IBL maps load failed failed");
            return;
        }

        outEnvNumLevels.push_back(ibl.numLevels);

        // Irradiance
        {
           MetalTexture texture;
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.irradianceMap.GetWidth(),
                ibl.irradianceMap.GetHeight(),
                MTL::PixelFormatRGBA32Float,
                ibl.irradianceMap.GetSizeInBytes(),
                ibl.irradianceMap.GetPixels(),
                &texture));
            outIrradianceTextures.push_back(texture);
        }

        // Environment
        {
            const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
            const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

            std::vector<MipOffset> mipOffsets;
            uint32_t               levelOffset = 0;
            uint32_t               levelWidth  = ibl.baseWidth;
            uint32_t               levelHeight = ibl.baseHeight;
            for (uint32_t i = 0; i < ibl.numLevels; ++i)
            {
                MipOffset mipOffset = {};
                mipOffset.Offset    = levelOffset;
                mipOffset.RowStride = rowStride;

                mipOffsets.push_back(mipOffset);

                levelOffset += (rowStride * levelHeight);
                levelWidth >>= 1;
                levelHeight >>= 1;
            }

           MetalTexture texture;
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.baseWidth,
                ibl.baseHeight,
                MTL::PixelFormatRGBA32Float,
                    mipOffsets,
                ibl.environmentMap.GetSizeInBytes(),
                ibl.environmentMap.GetPixels(),
                &texture));
            outEnvironmentTextures.push_back(texture);
        }

        gIBLNames.push_back(iblFile.filename().replace_extension().string());

        GREX_LOG_INFO("Loaded " << iblFile);
    }
}
