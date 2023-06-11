
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "mt_renderer.h"

// =================================================================================================
// MetalRenderer
// =================================================================================================

const int MetalRenderer::kMaxFramesInFlight = 3;

MetalRenderer::MetalRenderer()
{
}

MetalRenderer::~MetalRenderer()
{
    if (Swapchain != nullptr) {
        Swapchain->release();
        Swapchain = nullptr;
    }

    if (Queue != nullptr) {
        Queue->release();
        Queue = nullptr;
    }

    if (Device != nullptr) {
        Device->release();
        Device = nullptr;
    }
}

bool InitMetal(
    MetalRenderer* pRenderer,
    bool           enableDebug)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    pRenderer->DebugEnabled = enableDebug;

    pRenderer->Device = MTL::CreateSystemDefaultDevice();

    pRenderer->Queue = pRenderer->Device->newCommandQueue();

    pRenderer->Fence = dispatch_semaphore_create(MetalRenderer::kMaxFramesInFlight);

    return true;
}

bool InitSwapchain(
    MetalRenderer* pRenderer,
    void*          cocoaWindow,
    uint32_t       width,
    uint32_t       height)
{
    CGRect frame = (CGRect){
        {0,            0            },
        {(float)width, (float)height}
    };

    pRenderer->Swapchain = MTK::View::alloc()->init(frame, pRenderer->Device);
    pRenderer->Swapchain->setColorPixelFormat(GREX_DEFAULT_RTV_FORMAT);
    pRenderer->Swapchain->setPaused(false);
    pRenderer->Swapchain->setEnableSetNeedsDisplay(false);

    NS::Window* nsWindow = reinterpret_cast<NS::Window*>(cocoaWindow);
    nsWindow->setContentView(pRenderer->Swapchain);

    return true;
}

NS::Error* CreateBuffer(
    MetalRenderer* pRenderer,
    size_t         srcSize,
    const void*    pSrcData,
    MTL::Buffer**  ppResource)
{
    MTL::Buffer* pBuffer = pRenderer->Device->newBuffer(srcSize, MTL::ResourceStorageModeManaged);

    memcpy(pBuffer->contents(), pSrcData, srcSize);

    pBuffer->didModifyRange(NS::Range::Make(0, pBuffer->length()));

    *ppResource = pBuffer;

    return nullptr;
}

NS::Error* CreateDrawVertexColorPipeline(
    MetalRenderer*             pRenderer,
    MTL::Function*             vsShaderModule,
    MTL::Function*             fsShaderModule,
    MTL::PixelFormat           rtvFormat,
    MTL::PixelFormat           dsvFormat,
    MTL::RenderPipelineState** ppPipeline)
{
    MTL::VertexDescriptor* vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
    {
        // Position Buffer
        {
            MTL::VertexAttributeDescriptor* vertexAttribute = MTL::VertexAttributeDescriptor::alloc()->init();
            vertexAttribute->setOffset(0);
            vertexAttribute->setFormat(MTL::VertexFormatFloat3);
            vertexAttribute->setBufferIndex(0);
            vertexDescriptor->attributes()->setObject(vertexAttribute, 0);

            MTL::VertexBufferLayoutDescriptor* vertexBufferLayout = MTL::VertexBufferLayoutDescriptor::alloc()->init();
            vertexBufferLayout->setStride(12);
            vertexBufferLayout->setStepRate(1);
            vertexBufferLayout->setStepFunction(MTL::VertexStepFunctionPerVertex);
            vertexDescriptor->layouts()->setObject(vertexBufferLayout, 0);
        }
        // Vertex Color Buffer
        {
            MTL::VertexAttributeDescriptor* vertexAttribute = MTL::VertexAttributeDescriptor::alloc()->init();
            vertexAttribute->setOffset(0);
            vertexAttribute->setFormat(MTL::VertexFormatFloat3);
            vertexAttribute->setBufferIndex(1);
            vertexDescriptor->attributes()->setObject(vertexAttribute, 1);

            MTL::VertexBufferLayoutDescriptor* vertexBufferLayout = MTL::VertexBufferLayoutDescriptor::alloc()->init();
            vertexBufferLayout->setStride(12);
            vertexBufferLayout->setStepRate(1);
            vertexBufferLayout->setStepFunction(MTL::VertexStepFunctionPerVertex);
            vertexDescriptor->layouts()->setObject(vertexBufferLayout, 1);
        }
    }

    MTL::RenderPipelineDescriptor* pDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    pDesc->setVertexFunction(vsShaderModule);
    pDesc->setFragmentFunction(fsShaderModule);
    pDesc->colorAttachments()->object(0)->setPixelFormat(rtvFormat);
    pDesc->setVertexDescriptor(vertexDescriptor);

    NS::Error*                pError = nullptr;
    MTL::RenderPipelineState* pPSO   = pRenderer->Device->newRenderPipelineState(pDesc, &pError);

    pDesc->release();
    vertexDescriptor->release();

    *ppPipeline = pPSO;
    return pError;
}
