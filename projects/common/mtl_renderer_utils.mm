#include "mtl_renderer_utils.h"	

#include <Cocoa/Cocoa.h>	
#include <QuartzCore/QuartzCore.h>	

void MetalSetNSWindowSwapchain(	
   void* cocoaWindow,	
   void* caMetalLayer)	
{	
   NSWindow* nsWindow = reinterpret_cast<NSWindow*>(cocoaWindow);	
   CAMetalLayer* swapchain = reinterpret_cast<CAMetalLayer*>(caMetalLayer);	

   nsWindow.contentView.layer = swapchain;	
   nsWindow.contentView.wantsLayer = YES;	
}
