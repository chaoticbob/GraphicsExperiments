#pragma once	

#ifdef TARGET_IOS
void* MetalGetMetalLayer();
#else
void MetalSetNSWindowSwapchain(void* pCocoaWindow, void* pCAMetalLayer);
#endif
