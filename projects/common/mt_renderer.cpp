
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include "QuartzCore/QuartzCore.hpp"

#include "mt_renderer.h"
#include "mt_renderer_utils.h"

// NOCHECKIN We should put this in the header
#define GREX_DEFAULT_RTV_FORMAT MTL::PixelFormatBGRA8Unorm

// =================================================================================================
// MetalRenderer
// =================================================================================================
MetalRenderer::MetalRenderer()
{
}

MetalRenderer::~MetalRenderer()
{
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
    pRenderer->Device       = MTL::CreateSystemDefaultDevice();

    pRenderer->Queue = pRenderer->Device->newCommandQueue();

    return true;
}

bool InitSwapchain(
    MetalRenderer* pRenderer,
    void*          cocoaWindow,
    uint32_t       width,
    uint32_t       height)
{
	CA::MetalLayer* layer = CA::MetalLayer::layer();
    layer->setDevice(pRenderer->Device);
    layer->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
	layer->setDrawableSize(CGSizeMake(width, height));

   pRenderer->Swapchain = layer;

   MetalSetNSWindowSwapchain(cocoaWindow, pRenderer->Swapchain);
   
   return true;
}
