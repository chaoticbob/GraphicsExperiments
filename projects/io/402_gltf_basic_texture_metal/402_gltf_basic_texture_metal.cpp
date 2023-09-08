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

#define CAMERA_REGISTER                 4
#define DRAW_REGISTER                   5
#define INSTANCE_BUFFER_REGISTER        6
#define MATERIAL_BUFFER_REGISTER        7
#define MATERIAL_SAMPLER_START_REGISTER 8
#define MATERIAL_IMAGES_START_REGISTER  9

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth      = 1280;
static uint32_t gWindowHeight     = 720;
static bool     gEnableDebug      = true;
static bool     gEnableRayTracing = false;

static std::vector<std::string> gIBLNames = {};

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

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
    std::string shaderSource = LoadString("faux_render_shaders/render_base_color.metal");

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
    if (!FauxRender::LoadGLTF(GetAssetPath("scenes/basic_texture.gltf"), {}, &graph))
    {
        assert(false && "LoadGLTF failed");
        return EXIT_FAILURE;
    }
    if (!graph.InitializeResources())
    {
        assert(false && "Graph resources initialization failed");
        return EXIT_FAILURE;
    }

    graph.RootParameterIndices.Camera         = CAMERA_REGISTER;
    graph.RootParameterIndices.Draw           = DRAW_REGISTER;
    graph.RootParameterIndices.InstanceBuffer = INSTANCE_BUFFER_REGISTER;
    graph.RootParameterIndices.MaterialBuffer = MATERIAL_BUFFER_REGISTER;

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
    // Texture Arrays
    // *************************************************************************
    MTL::Buffer*               pMaterialImagesArgBuffer = nullptr;
    std::vector<MTL::Texture*> materialImagesTextures;

    MTL::Buffer*                    pMaterialSamplersArgBuffer = nullptr;
    std::vector<MTL::SamplerState*> materialSamplerStates;
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
                    materialSamplerStates.push_back(clampedSampler);
                    samplerCount++;

                    clampedSampler->release();
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
                    materialSamplerStates.push_back(repeatSampler);
                    samplerCount++;

                    repeatSampler->release();
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
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "402_gltf_basic_texture_metal");
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

       for (size_t i = 0; i < materialImagesTextures.size(); ++i)
       {
          pRenderEncoder->useResource(materialImagesTextures[i], MTL::ResourceUsageRead);
       }
        pRenderEncoder->setVertexBuffer(pMaterialImagesArgBuffer, 0, MATERIAL_IMAGES_START_REGISTER);
        pRenderEncoder->setFragmentBuffer(pMaterialImagesArgBuffer, 0, MATERIAL_IMAGES_START_REGISTER);

       for (size_t i = 0; i < materialSamplerStates.size(); ++i)
       {
          // pRenderEncoder->useResource(materialSamplerStates[i], MTL::ResourceUsageSample);
       }
        pRenderEncoder->setVertexBuffer(pMaterialSamplersArgBuffer, 0, MATERIAL_SAMPLER_START_REGISTER);
        pRenderEncoder->setFragmentBuffer(pMaterialSamplersArgBuffer, 0, MATERIAL_SAMPLER_START_REGISTER);

        // Draw scene
        const auto& scene = graph.Scenes[0];
        MtlFauxRender::Draw(&graph, scene.get(), pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}
