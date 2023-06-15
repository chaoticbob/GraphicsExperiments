
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
    SwapchainBufferCount = 0;
}

bool InitMetal(
    MetalRenderer* pRenderer,
    bool           enableDebug)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    pRenderer->DebugEnabled = enableDebug;

    pRenderer->Device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (pRenderer->Device.get() == nullptr) {
        assert(false && "MTL::CreateSystemDefaultDevice() failed");
        return false;
    }

    pRenderer->Queue = NS::TransferPtr(pRenderer->Device->newCommandQueue());
    if (pRenderer->Queue.get() == nullptr) {
        assert(false && "MTL::Device::newCommandQueue() failed");
        return false;
    }

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

    // Don't need to Retain/Release the swapchain as the function doesn't match the
    // naming template given in the metal-cpp README.md
    CA::MetalLayer* layer = CA::MetalLayer::layer();

    if (layer != nullptr) {
        layer->setDevice(pRenderer->Device.get());
        layer->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
        layer->setDrawableSize(layerSize);

        MetalSetNSWindowSwapchain(cocoaWindow, layer);

        pRenderer->Swapchain            = layer;
        pRenderer->SwapchainBufferCount = bufferCount;

        if (dsvFormat != MTL::PixelFormatInvalid) {
            for (int dsvIndex = 0; (dsvIndex < bufferCount); dsvIndex++) {
                auto depthBufferDesc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());

                depthBufferDesc->setPixelFormat(dsvFormat);
                depthBufferDesc->setWidth(width);
                depthBufferDesc->setHeight(height);
                depthBufferDesc->setMipmapLevelCount(1);
                depthBufferDesc->setResourceOptions(MTL::ResourceStorageModePrivate);
                depthBufferDesc->setUsage(MTL::TextureUsageRenderTarget);

                auto localTexture = NS::TransferPtr(pRenderer->Device->newTexture(depthBufferDesc.get()));

                if (localTexture.get() != nullptr) {
                    pRenderer->SwapchainDSVBuffers.push_back(localTexture);
                }
                else {
                    assert(false && "Depth Buffer MTL::Device::newTexture() failed");
                    return false;
                }
            }
        }
    }
    else {
        assert(false && "Swapchain creation CA::MetalLayer::layer() failed");
        return false;
    }

    return true;
}

NS::Error* CreateBuffer(
    MetalRenderer* pRenderer,
    size_t         srcSize,
    const void*    pSrcData,
    MetalBuffer*   pBuffer)
{
    pBuffer->Buffer = NS::TransferPtr(pRenderer->Device->newBuffer(srcSize, MTL::ResourceStorageModeManaged));

    if (pBuffer->Buffer.get() != nullptr) {
        memcpy(pBuffer->Buffer->contents(), pSrcData, srcSize);
        pBuffer->Buffer->didModifyRange(NS::Range::Make(0, pBuffer->Buffer->length()));
    }
    else {
        assert(false && "CreateBuffer() - MTL::Device::newBuffer() failed");
    }

    return nullptr;
}

NS::Error* CreateDrawVertexColorPipeline(
    MetalRenderer*            pRenderer,
    MetalShader*              pVsShaderModule,
    MetalShader*              pFsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipelineRenderState,
    MetalDepthStencilState*   pDepthStencilState)
{
    auto vertexDescriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
    if (vertexDescriptor.get() != nullptr) {
        // Position Buffer
        {
            MTL::VertexAttributeDescriptor* vertexAttribute = vertexDescriptor->attributes()->object(0);
            vertexAttribute->setOffset(0);
            vertexAttribute->setFormat(MTL::VertexFormatFloat3);
            vertexAttribute->setBufferIndex(0);

            MTL::VertexBufferLayoutDescriptor* vertexBufferLayout = vertexDescriptor->layouts()->object(0);
            vertexBufferLayout->setStride(12);
            vertexBufferLayout->setStepRate(1);
            vertexBufferLayout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }
        // Vertex Color Buffer
        {
            MTL::VertexAttributeDescriptor* vertexAttribute = vertexDescriptor->attributes()->object(1);
            vertexAttribute->setOffset(0);
            vertexAttribute->setFormat(MTL::VertexFormatFloat3);
            vertexAttribute->setBufferIndex(1);

            MTL::VertexBufferLayoutDescriptor* vertexBufferLayout = vertexDescriptor->layouts()->object(1);
            vertexBufferLayout->setStride(12);
            vertexBufferLayout->setStepRate(1);
            vertexBufferLayout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }
    }
    else {
        assert(false && "CreateDrawVertexColorPipeline() - MTL::VertexDescriptor::alloc::init() failed");
        return nullptr;
    }

    auto desc = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());

    if (desc.get() != nullptr) {
        desc->setVertexFunction(pVsShaderModule->Function.get());
        desc->setFragmentFunction(pFsShaderModule->Function.get());
        desc->setVertexDescriptor(vertexDescriptor.get());
        desc->colorAttachments()->object(0)->setPixelFormat(rtvFormat);
        desc->setDepthAttachmentPixelFormat(dsvFormat);

        NS::Error* pError           = nullptr;
        pPipelineRenderState->State = NS::TransferPtr(pRenderer->Device->newRenderPipelineState(desc.get(), &pError));
        if (pPipelineRenderState->State.get() == nullptr) {
            assert(false && "CreateDrawVertexColorPipeline() - MTL::Device::newRenderPipelineState() failed");
            return pError;
        }
    }
    else {
        assert(false && "CreateDrawVertexColorPipeline() - MTL::RenderPipelineDescriptor::alloc()->init() failed");
        return nullptr;
    }

    auto depthStateDesc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());

    if (depthStateDesc.get() != nullptr) {
        depthStateDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
        depthStateDesc->setDepthWriteEnabled(true);

        pDepthStencilState->State = NS::TransferPtr(pRenderer->Device->newDepthStencilState(depthStateDesc.get()));
        if (pDepthStencilState->State.get() == nullptr) {
            assert(false && "CreateDrawVertexColorPipeline() - MTL::Device::newDepthStencilState() failed");
            return nullptr;
        }
    }

    return nullptr;
}
