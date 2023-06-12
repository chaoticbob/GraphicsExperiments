
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "mtl_renderer.h"
#include "mtl_renderer_utils.h"

// =================================================================================================
// MetalRenderer
// =================================================================================================

MetalRenderer::MetalRenderer()
{
}

MetalRenderer::~MetalRenderer()
{
    for (auto dsvBuffer : SwapchainDSVBuffers) {
        dsvBuffer->release();
    }
    SwapchainBufferCount = 0;

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

    return true;
}

bool InitSwapchain(
    MetalRenderer*   pRenderer,
    void*            cocoaWindow,
    uint32_t         width,
    uint32_t         height,
    uint32_t         bufferCount,
    MTL::PixelFormat dsvFormat)
{
    CGSize layerSize = {(float)width, (float)height};

    CA::MetalLayer* layer = CA::MetalLayer::layer();
    layer->setDevice(pRenderer->Device);
    layer->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
    layer->setDrawableSize(layerSize);

    MetalSetNSWindowSwapchain(cocoaWindow, layer);

    pRenderer->Swapchain = layer;

    if (dsvFormat != MTL::PixelFormatInvalid) {
        for (int dsvIndex = 0; dsvIndex < bufferCount; dsvIndex++) {
            MTL::TextureDescriptor* pDepthBufferDesc = MTL::TextureDescriptor::alloc()->init();

            pDepthBufferDesc->setPixelFormat(dsvFormat);
            pDepthBufferDesc->setWidth(width);
            pDepthBufferDesc->setHeight(height);
            pDepthBufferDesc->setMipmapLevelCount(1);
            pDepthBufferDesc->setResourceOptions(MTL::ResourceStorageModePrivate);
            pDepthBufferDesc->setUsage(MTL::TextureUsageRenderTarget);

            pRenderer->SwapchainDSVBuffers.push_back(pRenderer->Device->newTexture(pDepthBufferDesc));

            pDepthBufferDesc->release();
        }
    }

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
    MTL::RenderPipelineState** ppPipeline,
    MTL::DepthStencilState**   ppDepthStencilState)
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

    NS::Error* pError = nullptr;
    {
        MTL::RenderPipelineDescriptor* pDesc = MTL::RenderPipelineDescriptor::alloc()->init();
        pDesc->setVertexFunction(vsShaderModule);
        pDesc->setFragmentFunction(fsShaderModule);
        pDesc->setVertexDescriptor(vertexDescriptor);
        pDesc->colorAttachments()->object(0)->setPixelFormat(rtvFormat);
        pDesc->setDepthAttachmentPixelFormat(dsvFormat);

        *ppPipeline = pRenderer->Device->newRenderPipelineState(pDesc, &pError);

        pDesc->release();
    }

    if (pError == nullptr) {
        MTL::DepthStencilDescriptor* pDepthStateDesc = MTL::DepthStencilDescriptor::alloc()->init();
        pDepthStateDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
        pDepthStateDesc->setDepthWriteEnabled(true);

        *ppDepthStencilState = pRenderer->Device->newDepthStencilState(pDepthStateDesc);

        pDepthStateDesc->release();
    }

    vertexDescriptor->release();

    return pError;
}
