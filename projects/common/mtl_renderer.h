#pragma once

#include "config.h"

#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>

#define GREX_DEFAULT_RTV_FORMAT MTL::PixelFormatBGRA8Unorm_sRGB
#define GREX_DEFAULT_DSV_FORMAT MTL::PixelFormatDepth32Float

struct MetalRenderer
{
    bool                       DebugEnabled         = false;
    MTL::Device*               Device               = nullptr;
    MTL::CommandQueue*         Queue                = nullptr;
    CA::MetalLayer*            Swapchain            = nullptr;
    uint32_t                   SwapchainBufferCount = 0;
    std::vector<MTL::Texture*> SwapchainDSVBuffers  = {};

    MetalRenderer();
    ~MetalRenderer();
};

bool InitMetal(MetalRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(MetalRenderer* pRenderer, void* cocoaWindow, uint32_t width, uint32_t height, uint32_t bufferCount = 2, MTL::PixelFormat dsvFormat = MTL::PixelFormatInvalid);

NS::Error* CreateBuffer(MetalRenderer* pRenderer, size_t srcSize, const void* pSrcData, MTL::Buffer** ppResource);

NS::Error* CreateDrawVertexColorPipeline(
    MetalRenderer*             pRenderer,
    MTL::Function*             vsShaderModule,
    MTL::Function*             fsShaderModule,
    MTL::PixelFormat           rtvFormat,
    MTL::PixelFormat           dsvFormat,
    MTL::RenderPipelineState** ppPipeline,
    MTL::DepthStencilState**   ppDepthStencilState);
