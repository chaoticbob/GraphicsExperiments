#pragma once

#include "config.h"

#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>

#define GREX_DEFAULT_RTV_FORMAT MTL::PixelFormatBGRA8Unorm
#define GREX_DEFAULT_DSV_FORMAT MTL::PixelFormatDepth32Float

enum MtlPipelineFlags
{
    METAL_PIPELINE_FLAGS_INTERLEAVED_ATTRS = 0x00000001
};

struct MetalRenderer
{
    bool                                     DebugEnabled = false;
    NS::SharedPtr<MTL::Device>               Device;
    NS::SharedPtr<MTL::CommandQueue>         Queue;
    CA::MetalLayer*                          pSwapchain = nullptr;
    std::vector<NS::SharedPtr<MTL::Texture>> SwapchainDSVBuffers;
    uint32_t                                 SwapchainBufferCount = 0;

    MetalRenderer();
    ~MetalRenderer();
};

bool InitMetal(MetalRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(MetalRenderer* pRenderer, void* pCocoaWindow, uint32_t width, uint32_t height, uint32_t bufferCount = 2, MTL::PixelFormat dsvFormat = MTL::PixelFormatInvalid);

struct MetalBuffer
{
    NS::SharedPtr<MTL::Buffer> Buffer;
};

struct MetalTexture
{
    NS::SharedPtr<MTL::Texture> Texture;
};

struct MetalPipelineRenderState
{
    NS::SharedPtr<MTL::RenderPipelineState> State;
};

struct MetalDepthStencilState
{
    NS::SharedPtr<MTL::DepthStencilState> State;
};

struct MetalShader
{
    NS::SharedPtr<MTL::Function> Function;
};

NS::Error* CreateBuffer(MetalRenderer* pRenderer, size_t srcSize, const void* pSrcData, MetalBuffer* pBuffer);

NS::Error* CreateTexture(
    MetalRenderer*                pRenderer,
    uint32_t                      width,
    uint32_t                      height,
    MTL::PixelFormat              format,
    const std::vector<MipOffset>& mipOffsets,
    uint64_t                      srcSizeBytes,
    const void*                   pSrcData,
    MetalTexture*                 pResource);

NS::Error* CreateTexture(
    MetalRenderer*   pRenderer,
    uint32_t         width,
    uint32_t         height,
    MTL::PixelFormat format,
    uint64_t         srcSizeBytes,
    const void*      pSrcData,
    MetalTexture*    pResource);

NS::Error* CreateDrawVertexColorPipeline(
    MetalRenderer*              pRenderer,
    MetalShader*                vsShaderModule,
    MetalShader*                fsShaderModule,
    MTL::PixelFormat            rtvFormat,
    MTL::PixelFormat            dsvFormat,
    MetalPipelineRenderState*   pPipeline,
    MetalDepthStencilState*     pDepthStencilState,
    MTL::PrimitiveTopologyClass topologyType  = MTL::PrimitiveTopologyClassTriangle,
    uint32_t                    pipelineFlags = 0);

NS::Error* CreateDrawNormalPipeline(
    MetalRenderer*            pRenderer,
    MetalShader*              vsShaderModule,
    MetalShader*              fsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipeline,
    MetalDepthStencilState*   pDepthStencilState,
	bool                      enableTangents = false);

NS::Error* CreateDrawTexturePipeline(
    MetalRenderer*            pRenderer,
    MetalShader*              vsShaderModule,
    MetalShader*              fsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipeline,
    MetalDepthStencilState*   pDepthStencilState);

NS::Error* CreateDrawBasicPipeline(
    MetalRenderer*            pRenderer,
    MetalShader*              pVsShaderModule,
    MetalShader*              pFsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipelineRenderState,
    MetalDepthStencilState*   pDepthStencilState);

NS::Error* CreateGraphicsPipeline1(
    MetalRenderer*            pRenderer,
    MetalShader*              vsShaderModule,
    MetalShader*              fsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipeline,
    MetalDepthStencilState*   pDepthStencilState);
