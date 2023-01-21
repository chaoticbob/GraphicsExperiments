#pragma once

#include "config.h"

#include <d3d12.h>
#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <dxcapi.h>

struct DxRenderer
{
    bool                       DebugEnabled             = true;
    ComPtr<IDXGIFactory5>      Factory                  = nullptr;
    ComPtr<IDXGIAdapter4>      Adapter                  = nullptr;
    ComPtr<ID3D12Device7>      Device                   = nullptr;
    ComPtr<ID3D12Fence>        DeviceFence              = nullptr;
    UINT                       DeviceFenceValue         = 0;
    HANDLE                     DeviceWaitEventHandle    = nullptr;
    ComPtr<ID3D12CommandQueue> Queue                    = nullptr;
    ComPtr<IDXGISwapChain4>    Swapchain                = nullptr;
    ComPtr<ID3D12Fence>        SwapchainFence           = nullptr;
    UINT64                     SwapchainFenceValue      = 0;
    HANDLE                     SwapchainWaitEventHandle = nullptr;

    DxRenderer();
    ~DxRenderer();
};

bool InitDx(DxRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(DxRenderer* pRenderer, HWND hwnd, uint32_t width, uint32_t height, uint32_t bufferCount = 2);
bool WaitForGpu(DxRenderer* pRenderer);
bool SwapchainPresent(DxRenderer* pRenderer);

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, size_t minAlignment, ID3D12Resource** ppResource);
HRESULT CreateUAVBuffer(DxRenderer* pRenderer, size_t size, D3D12_RESOURCE_STATES initialResourceState, ID3D12Resource** ppResource);

D3D12_RESOURCE_BARRIER CreateTransition(
    ID3D12Resource*              pResource,
    D3D12_RESOURCE_STATES        StateBefore,
    D3D12_RESOURCE_STATES        StateAfter,
    UINT                         Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
    D3D12_RESOURCE_BARRIER_FLAGS Flags       = D3D12_RESOURCE_BARRIER_FLAG_NONE);