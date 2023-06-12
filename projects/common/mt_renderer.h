#pragma once

#include "config.h"

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#define GREX_DEFAULT_RTV_FORMAT MTL::PixelFormatBGRA8Unorm_sRGB
#define GREX_DEFAULT_DSV_FORMAT MTL::PixelFormatDepth32Float

struct MetalRenderer
{
    bool               DebugEnabled = false;
    MTL::Device*       Device       = nullptr;
    MTL::CommandQueue* Queue        = nullptr;
    MTK::View*         Swapchain    = nullptr;

    static const int kMaxFramesInFlight;

    MetalRenderer();
    ~MetalRenderer();
};

bool InitMetal(MetalRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(MetalRenderer* pRenderer, void* cocoaWindow, uint32_t width, uint32_t height);

NS::Error* CreateBuffer(MetalRenderer* pRenderer, size_t srcSize, const void* pSrcData, MTL::Buffer** ppResource);

NS::Error* CreateDrawVertexColorPipeline(
    MetalRenderer*             pRenderer,
    MTL::Function*             vsShaderModule,
    MTL::Function*             fsShaderModule,
    MTL::PixelFormat           rtvFormat,
    MTL::PixelFormat           dsvFormat,
    MTL::RenderPipelineState** ppPipeline,
    MTL::DepthStencilState**   ppDepthStencilState);
