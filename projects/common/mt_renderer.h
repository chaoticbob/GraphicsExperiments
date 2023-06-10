#pragma once

#include "config.h"

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

struct MetalRenderer
{
    bool               DebugEnabled = false;
    MTL::Device*       Device       = nullptr;
    MTL::CommandQueue* Queue        = nullptr;
    MTK::View*         Swapchain    = nullptr;

    MetalRenderer();
    ~MetalRenderer();
};

bool InitMetal(MetalRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(MetalRenderer* pRenderer, void* cocoaWindow, uint32_t width, uint32_t height);
