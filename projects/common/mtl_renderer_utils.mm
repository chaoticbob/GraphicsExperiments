#include "mtl_renderer_utils.h"

#if !TARGET_IOS
#include <Cocoa/Cocoa.h>
#include <QuartzCore/QuartzCore.h>

void MetalSetNSWindowSwapchain(
    void* pCocoaWindow,
    void* pCAMetalLayer)
{
    NSWindow*     pNSWindow  = reinterpret_cast<NSWindow*>(pCocoaWindow);
    CAMetalLayer* pSwapchain = reinterpret_cast<CAMetalLayer*>(pCAMetalLayer);

    pNSWindow.contentView.layer      = pSwapchain;
    pNSWindow.contentView.wantsLayer = YES;
}

#else // TARGET_IOS

#include "ios/AAPLViewController.h"

void* MetalGetMetalLayer()
{
   return (void*)getMetalLayer();
}

#endif
