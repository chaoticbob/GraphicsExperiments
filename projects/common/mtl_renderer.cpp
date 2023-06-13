
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

    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();
    bool                 success        = true;

    pRenderer->DebugEnabled = enableDebug;

    MTL::Device* localDevice = MTL::CreateSystemDefaultDevice();
    if (localDevice != nullptr) {
        pRenderer->Device = NS::TransferPtr(localDevice);
    }
    else {
        assert(false && "MTL::CreateSystemDefaultDevice() failed");
        success = false;
    }

    if (success) {
        MTL::CommandQueue* localCommandQueue = pRenderer->Device->newCommandQueue();
        if (localCommandQueue != nullptr) {
            pRenderer->Queue = NS::TransferPtr(localCommandQueue);
        }
        else {
            assert(false && "MTL::Device::newCommandQueue() failed");
            success = false;
        }
    }

    pPoolAllocator->release();

    return success;
}

bool InitSwapchain(
    MetalRenderer*   pRenderer,
    void*            cocoaWindow,
    uint32_t         width,
    uint32_t         height,
    uint32_t         bufferCount,
    MTL::PixelFormat dsvFormat)
{
    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();
    bool                 success        = true;

    CGSize layerSize = {(float)width, (float)height};

	// Don't need to Retain/Release the swapchain as the function doesn't match the 
	// naming templated named in metal-cpp README.md
    CA::MetalLayer* layer = CA::MetalLayer::layer();

    if (layer != nullptr) {
        layer->setDevice(pRenderer->Device.get());
        layer->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
        layer->setDrawableSize(layerSize);

        MetalSetNSWindowSwapchain(cocoaWindow, layer);

        pRenderer->Swapchain            = layer;
        pRenderer->SwapchainBufferCount = bufferCount;

        if (dsvFormat != MTL::PixelFormatInvalid) {
            for (int dsvIndex = 0; success && (dsvIndex < bufferCount); dsvIndex++) {
                MTL::TextureDescriptor* pDepthBufferDesc = MTL::TextureDescriptor::alloc()->init();

                pDepthBufferDesc->setPixelFormat(dsvFormat);
                pDepthBufferDesc->setWidth(width);
                pDepthBufferDesc->setHeight(height);
                pDepthBufferDesc->setMipmapLevelCount(1);
                pDepthBufferDesc->setResourceOptions(MTL::ResourceStorageModePrivate);
                pDepthBufferDesc->setUsage(MTL::TextureUsageRenderTarget);

                MTL::Texture* localTexture = pRenderer->Device->newTexture(pDepthBufferDesc);

                if (localTexture != nullptr) {
                    pRenderer->SwapchainDSVBuffers.push_back(NS::TransferPtr(localTexture));
                }
                else {
                    assert(false && "Depth Buffer MTL::Device::newTexture() failed");
                    success = false;
                }

                pDepthBufferDesc->release();
            }
        }
    }
    else {
        assert(false && "Swapchain creation CA::MetalLayer::layer() failed");
        success = false;
    }

    pPoolAllocator->release();

    return success;
}

NS::Error* CreateBuffer(
    MetalRenderer* pRenderer,
    size_t         srcSize,
    const void*    pSrcData,
    MetalBuffer*   pBuffer)
{
    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();

    MTL::Buffer* pLocalBuffer = pRenderer->Device->newBuffer(srcSize, MTL::ResourceStorageModeManaged);

    if (pLocalBuffer != nullptr) {
        pBuffer->Buffer = NS::TransferPtr(pLocalBuffer);

        memcpy(pBuffer->Buffer->contents(), pSrcData, srcSize);

        pBuffer->Buffer->didModifyRange(NS::Range::Make(0, pBuffer->Buffer->length()));
    }
    else {
        assert(false && "CreateBuffer() - MTL::Device::newBuffer() failed");
    }

    pPoolAllocator->release();

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
    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();

    MTL::VertexDescriptor* pVertexDescriptor = MTL::VertexDescriptor::alloc()->init();
    if (pVertexDescriptor != nullptr) {
        // Position Buffer
        {
            MTL::VertexAttributeDescriptor* vertexAttribute = pVertexDescriptor->attributes()->object(0);
            vertexAttribute->setOffset(0);
            vertexAttribute->setFormat(MTL::VertexFormatFloat3);
            vertexAttribute->setBufferIndex(0);

            MTL::VertexBufferLayoutDescriptor* vertexBufferLayout = pVertexDescriptor->layouts()->object(0);
            vertexBufferLayout->setStride(12);
            vertexBufferLayout->setStepRate(1);
            vertexBufferLayout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }
        // Vertex Color Buffer
        {
            MTL::VertexAttributeDescriptor* vertexAttribute = pVertexDescriptor->attributes()->object(1);
            vertexAttribute->setOffset(0);
            vertexAttribute->setFormat(MTL::VertexFormatFloat3);
            vertexAttribute->setBufferIndex(1);

            MTL::VertexBufferLayoutDescriptor* vertexBufferLayout = pVertexDescriptor->layouts()->object(1);
            vertexBufferLayout->setStride(12);
            vertexBufferLayout->setStepRate(1);
            vertexBufferLayout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }
    }
    else {
        assert(false && "CreateDrawVertexColorPipeline() - MTL::VertexDescriptor::alloc::init() failed");
    }

    NS::Error* pError = nullptr;
    {
        MTL::RenderPipelineDescriptor* pDesc = MTL::RenderPipelineDescriptor::alloc()->init();

        if (pDesc != nullptr) {
            pDesc->setVertexFunction(pVsShaderModule->Function.get());
            pDesc->setFragmentFunction(pFsShaderModule->Function.get());
            pDesc->setVertexDescriptor(pVertexDescriptor);
            pDesc->colorAttachments()->object(0)->setPixelFormat(rtvFormat);
            pDesc->setDepthAttachmentPixelFormat(dsvFormat);

            MTL::RenderPipelineState* pLocalPipelineState = pRenderer->Device->newRenderPipelineState(pDesc, &pError);
            if (pLocalPipelineState != nullptr) {
                pPipelineRenderState->State = NS::TransferPtr(pLocalPipelineState);
            }
            else {
                assert(false && "CreateDrawVertexColorPipeline() - MTL::Device::newRenderPipelineState() failed");
            }

            pDesc->release();
        }
        else {
            assert(false && "CreateDrawVertexColorPipeline() - MTL::RenderPipelineDescriptor::alloc()->init() failed");
        }
    }

    if (pError == nullptr) {
        MTL::DepthStencilDescriptor* pDepthStateDesc = MTL::DepthStencilDescriptor::alloc()->init();

        if (pDepthStateDesc != nullptr) {
            pDepthStateDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
            pDepthStateDesc->setDepthWriteEnabled(true);

            MTL::DepthStencilState* pLocalDepthState = pRenderer->Device->newDepthStencilState(pDepthStateDesc);
            if (pLocalDepthState != nullptr) {
                pDepthStencilState->State = NS::TransferPtr(pLocalDepthState);
            }
            else {
                assert(false && "CreateDrawVertexColorPipeline() - MTL::Device::newDepthStencilState() failed");
            }

            pDepthStateDesc->release();
        }
    }

    pVertexDescriptor->release();
    pPoolAllocator->release();

    return pError;
}
