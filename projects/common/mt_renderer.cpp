
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "mt_renderer.h"

// NOCHECKIN We should put this in the header
#define GREX_DEFAULT_RTV_FORMAT MTL::PixelFormatBGRA8Unorm_sRGB

// =================================================================================================
// MetalRenderer
// =================================================================================================
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

    NS::Window* nsWindow = reinterpret_cast<NS::Window*>(cocoaWindow);
    nsWindow->setContentView(pRenderer->Swapchain);

    return true;
}
