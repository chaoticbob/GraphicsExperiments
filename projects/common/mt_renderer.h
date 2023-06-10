#pragma once

#include "config.h"

#include <Metal/Metal.hpp>
#include "QuartzCore/QuartzCore.hpp"

struct MetalRenderer
{
    bool               DebugEnabled = false;
    MTL::Device*       Device       = nullptr;
    MTL::CommandQueue* Queue        = nullptr;
    CA::MetalLayer*    Swapchain    = nullptr;

    MetalRenderer();
    ~MetalRenderer();
};

bool InitMetal(MetalRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(MetalRenderer* pRenderer, void* cocoaWindow, uint32_t width, uint32_t height);
