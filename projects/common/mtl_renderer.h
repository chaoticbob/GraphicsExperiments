#pragma once

#include "config.h"

#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>

#define GREX_DEFAULT_RTV_FORMAT MTL::PixelFormatBGRA8Unorm_sRGB
#define GREX_DEFAULT_DSV_FORMAT MTL::PixelFormatDepth32Float

struct MetalRenderer
{
    bool                                     DebugEnabled = false;
    NS::SharedPtr<MTL::Device>               Device;
    NS::SharedPtr<MTL::CommandQueue>         Queue;
    CA::MetalLayer*                          Swapchain = nullptr;
    std::vector<NS::SharedPtr<MTL::Texture>> SwapchainDSVBuffers;
    uint32_t                                 SwapchainBufferCount = 0;

    MetalRenderer();
    ~MetalRenderer();
};

bool InitMetal(MetalRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(MetalRenderer* pRenderer, void* cocoaWindow, uint32_t width, uint32_t height, uint32_t bufferCount = 2, MTL::PixelFormat dsvFormat = MTL::PixelFormatInvalid);

struct MetalBuffer
{
    NS::SharedPtr<MTL::Buffer> Buffer;
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

NS::Error* CreateDrawVertexColorPipeline(
    MetalRenderer*            pRenderer,
    MetalShader*              vsShaderModule,
    MetalShader*              fsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipeline,
    MetalDepthStencilState*   pDepthStencilState);

NS::Error* CreateDrawNormalPipeline(
    MetalRenderer*            pRenderer,
    MetalShader*              vsShaderModule,
    MetalShader*              fsShaderModule,
    MTL::PixelFormat          rtvFormat,
    MTL::PixelFormat          dsvFormat,
    MetalPipelineRenderState* pPipeline,
    MetalDepthStencilState*   pDepthStencilState);