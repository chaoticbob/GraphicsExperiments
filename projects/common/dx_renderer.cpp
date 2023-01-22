#include "dx_renderer.h"

DxRenderer::DxRenderer()
{
}

DxRenderer::~DxRenderer()
{
    if (SwapchainWaitEventHandle != nullptr) {
        CloseHandle(SwapchainWaitEventHandle);
        SwapchainWaitEventHandle = nullptr;
    }

    if (DeviceWaitEventHandle != nullptr) {
        CloseHandle(DeviceWaitEventHandle);
        DeviceWaitEventHandle = nullptr;
    }
}

bool InitDx(DxRenderer* pRenderer, bool enableDebug)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    pRenderer->DebugEnabled = enableDebug;

    // Debug
    if (pRenderer->DebugEnabled) {
        // Get DXGI debug interface
        ComPtr<IDXGIDebug1> dxgiDebug;
        HRESULT             hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));
        if (FAILED(hr)) {
            assert(false && "DXGIGetDebugInterface1(DXGIDebug) failed");
            return false;
        }

        ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
        // Get DXGI info queue
        hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue));
        if (FAILED(hr)) {
            assert(false && "DXGIGetDebugInterface1(DXGIInfoQueue) failed");
            return false;
        }

        // Set breaks
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

        // Get D3D12 debug interface
        ComPtr<ID3D12Debug> d3d12Debug;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12Debug));
        if (FAILED(hr)) {
            assert(false && "D3D12GetDebugInterface failed");
            return false;
        }
        // Enable debug layers
        d3d12Debug->EnableDebugLayer();
    }

    // Factory
    {
        UINT    flags = pRenderer->DebugEnabled ? DXGI_CREATE_FACTORY_DEBUG : 0;
        HRESULT hr    = CreateDXGIFactory2(flags, IID_PPV_ARGS(&pRenderer->Factory));
        if (FAILED(hr)) {
            assert(false && "DXGI factory creation failed");
            return false;
        }
    }

    // Adapter
    {
        std::vector<IDXGIAdapter1*> adapters;
        UINT                        adapterIndex       = 0;
        IDXGIAdapter1*              pEnumeratedAdapter = nullptr;
        while (pRenderer->Factory->EnumAdapters1(adapterIndex, &pEnumeratedAdapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc = {};
            HRESULT            hr   = pEnumeratedAdapter->GetDesc1(&desc);
            if (FAILED(hr)) {
                assert(false && "IDXGIAdapter1::GetDesc1 failed");
                return false;
            }

            // Filter out remote and software adapters
            if (desc.Flags == DXGI_ADAPTER_FLAG_NONE) {
                adapters.push_back(pEnumeratedAdapter);
            }

            ++adapterIndex;
        }

        if (adapters.empty()) {
            assert(false && "No adapters found");
            return false;
        }

        HRESULT hr = adapters[0]->QueryInterface(IID_PPV_ARGS(&pRenderer->Adapter));
        if (FAILED(hr)) {
            assert(false && "IDXGIAdapter1::QueryInterface failed");
            return false;
        }
    }

    // Device
    {
        DXGI_ADAPTER_DESC3 desc = {};
        pRenderer->Adapter->GetDesc3(&desc);

        HRESULT hr = D3D12CreateDevice(
            pRenderer->Adapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&pRenderer->Device));
        if (FAILED(hr)) {
            assert(false && "D3D12CreateDevice failed");
            return false;
        }

        char description[128];
        memset(description, 0, 128);
        for (size_t i = 0; i < 128; ++i) {
            WCHAR c        = desc.Description[i];
            description[i] = static_cast<char>(c);
            if (c == 0) {
                break;
            }
        }

        GREX_LOG_INFO("Created device using " << description);
    }

    // Device fence
    {
        HRESULT hr = pRenderer->Device->CreateFence(
            pRenderer->DeviceFenceValue,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&pRenderer->DeviceFence));
        if (FAILED(hr)) {
            assert(false && "ID3D12::CreateFence failed");
            return false;
        }
    }

    // Wait event
    DWORD eventFlags                 = 0;
    pRenderer->DeviceWaitEventHandle = CreateEventEx(NULL, NULL, eventFlags, EVENT_ALL_ACCESS);
    if (pRenderer->DeviceWaitEventHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Queue
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Priority                 = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;

        HRESULT hr = pRenderer->Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&pRenderer->Queue));
        if (FAILED(hr)) {
            assert(false && "ID3D12Device::CreateCommandQueue failed");
            return false;
        }
    }

    // Imgui descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors             = 1;
        desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        HRESULT hr = pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pRenderer->ImGuiFontDescriptorHeap));
        if (FAILED(hr)) {
            assert(false && "ID3D12Device::CreateDescriptorHeap failed");
            return false;
        }
    }

    return true;
}

bool InitSwapchain(DxRenderer* pRenderer, HWND hwnd, uint32_t width, uint32_t height, uint32_t bufferCount)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    pRenderer->SwapchainRTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width                 = width;
    desc.Height                = height;
    desc.Format                = pRenderer->SwapchainRTVFormat;
    desc.Stereo                = FALSE;
    desc.SampleDesc            = {1, 0};
    desc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT;
    desc.BufferCount           = bufferCount;
    desc.Scaling               = DXGI_SCALING_NONE;
    desc.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags                 = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    IDXGISwapChain1* pSwapchain = nullptr;
    //
    HRESULT hr = pRenderer->Factory->CreateSwapChainForHwnd(
        pRenderer->Queue.Get(), // pDevice
        hwnd,                   // hWnd
        &desc,                  // pDesc
        nullptr,                // pFullscreenDesc
        nullptr,                // pRestrictToOuput
        &pSwapchain);           // ppSwapchain
    if (FAILED(hr)) {
        assert(false && "IDXGIFactory::CreateSwapChain failed");
        return false;
    }

    hr = pSwapchain->QueryInterface(IID_PPV_ARGS(&pRenderer->Swapchain));
    if (FAILED(hr)) {
        assert(false && "IDXGISwapChain1::QueryInterface failed");
        return false;
    }

    // Get buffer count from swapchain
    {
        DXGI_SWAP_CHAIN_DESC1 postCreateDesc = {};

        hr = pRenderer->Swapchain->GetDesc1(&postCreateDesc);
        if (FAILED(hr)) {
            assert(false && "IDXGISwapChain1::GetDesc1 failed");
            return false;
        }

        pRenderer->SwapchainBufferCount = postCreateDesc.BufferCount;
    }

    // Create swapchain RTVs
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors             = pRenderer->SwapchainBufferCount;
        desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pRenderer->SwapchainRTVDescriptorHeap));
        if (FAILED(hr)) {
            assert(false && "ID3D12Device::CreateDescriptorHeap failed");
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = pRenderer->SwapchainRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < pRenderer->SwapchainBufferCount; ++i) {
            ID3D12Resource* pSwapchainBuffer = nullptr;

            hr = pRenderer->Swapchain->GetBuffer(i, IID_PPV_ARGS(&pSwapchainBuffer));
            if (FAILED(hr)) {
                assert(false && "IDXGISwapChain1::GetBuffer failed");
                return false;
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format                        = pRenderer->SwapchainRTVFormat;
            rtvDesc.ViewDimension                 = D3D12_RTV_DIMENSION_TEXTURE2D;

            pRenderer->Device->CreateRenderTargetView(pSwapchainBuffer, &rtvDesc, rtv);
            pRenderer->SwapchainRTVDescriptorHandles.push_back(rtv);

            rtv.ptr += pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
    }

    hr = pRenderer->Device->CreateFence(
        pRenderer->SwapchainFenceValue,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&pRenderer->SwapchainFence));
    if (FAILED(hr)) {
        assert(false && "ID3D12Device::CreateFence failed");
        return false;
    }

    // Wait event
    DWORD eventFlags                    = 0;
    pRenderer->SwapchainWaitEventHandle = CreateEventEx(NULL, NULL, eventFlags, EVENT_ALL_ACCESS);
    if (pRenderer->SwapchainWaitEventHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    return true;
}

bool WaitForGpu(DxRenderer* pRenderer)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    ++pRenderer->DeviceFenceValue;
    HRESULT hr = pRenderer->Queue->Signal(
        pRenderer->DeviceFence.Get(),
        pRenderer->DeviceFenceValue);
    if (FAILED(hr)) {
        assert(false && "ID3D12Fence::Signal failed");
        return false;
    }

    hr = pRenderer->DeviceFence->SetEventOnCompletion(
        pRenderer->DeviceFenceValue,
        pRenderer->DeviceWaitEventHandle);
    if (FAILED(hr)) {
        assert(false && "ID3D12Fence::SetEventOnCompletion failed");
        return false;
    }

    DWORD res = WaitForSingleObjectEx(pRenderer->DeviceWaitEventHandle, INFINITE, false);
    if (res != WAIT_OBJECT_0) {
        assert(false && "WaitForSingleObjectEx failed");
        return false;
    }

    return true;
}

bool SwapchainPresent(DxRenderer* pRenderer)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    HRESULT hr = pRenderer->Swapchain->Present(0, 0);
    if (FAILED(hr)) {
        assert(false && "IDXGISwapChain::Present failed");
        return false;
    }

    ++pRenderer->SwapchainFenceValue;
    hr = pRenderer->Queue->Signal(
        pRenderer->SwapchainFence.Get(),
        pRenderer->SwapchainFenceValue);
    if (FAILED(hr)) {
        assert(false && "ID3D12Fence::Signal failed");
        return false;
    }

    hr = pRenderer->SwapchainFence->SetEventOnCompletion(
        pRenderer->SwapchainFenceValue,
        pRenderer->SwapchainWaitEventHandle);
    if (FAILED(hr)) {
        assert(false && "ID3D12Fence::SetEventOnCompletion failed");
        return false;
    }

    DWORD res = WaitForSingleObjectEx(pRenderer->SwapchainWaitEventHandle, INFINITE, false);
    if (res != WAIT_OBJECT_0) {
        assert(false && "WaitForSingleObjectEx failed");
        return false;
    }

    return true;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, ID3D12Resource** ppResource)
{
    if (IsNull(pRenderer)) {
        return E_UNEXPECTED;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment           = 0;
    desc.Width               = srcSize;
    desc.Height              = 1;
    desc.DepthOrArraySize    = 1;
    desc.MipLevels           = 1;
    desc.Format              = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc          = {1, 0};
    desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = D3D12_HEAP_TYPE_UPLOAD;

    HRESULT hr = pRenderer->Device->CreateCommittedResource(
        &heapProperties,                   // pHeapProperties
        D3D12_HEAP_FLAG_NONE,              // HeapFlags
        &desc,                             // pDesc
        D3D12_RESOURCE_STATE_GENERIC_READ, // InitialResourceState
        nullptr,                           // pOptimizedClearValues
        IID_PPV_ARGS(ppResource));         // riidResource, ppvResouce
    if (FAILED(hr)) {
        return hr;
    }

    if (!IsNull(pSrcData)) {
        void* pData = nullptr;
        hr          = (*ppResource)->Map(0, nullptr, &pData);
        if (FAILED(hr)) {
            return hr;
        }

        memcpy(pData, pSrcData, srcSize);

        (*ppResource)->Unmap(0, nullptr);
    }

    return S_OK;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, size_t minAlignment, ID3D12Resource** ppResource)
{
    if (minAlignment > 0) {
        srcSize = Align<size_t>(srcSize, minAlignment);
    }

    return CreateBuffer(pRenderer, srcSize, pSrcData, ppResource);
}

HRESULT CreateUAVBuffer(DxRenderer* pRenderer, size_t size, D3D12_RESOURCE_STATES initialResourceState, ID3D12Resource** ppResource)
{
    if (IsNull(pRenderer)) {
        return E_UNEXPECTED;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment           = 0;
    desc.Width               = size;
    desc.Height              = 1;
    desc.DepthOrArraySize    = 1;
    desc.MipLevels           = 1;
    desc.Format              = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc          = {1, 0};
    desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags               = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = pRenderer->Device->CreateCommittedResource(
        &heapProperties,           // pHeapProperties
        D3D12_HEAP_FLAG_NONE,      // HeapFlags
        &desc,                     // pDesc
        initialResourceState,      // InitialResourceState
        nullptr,                   // pOptimizedClearValues
        IID_PPV_ARGS(ppResource)); // riidResource, ppvResouce
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

D3D12_RESOURCE_BARRIER CreateTransition(
    ID3D12Resource*              pResource,
    D3D12_RESOURCE_STATES        StateBefore,
    D3D12_RESOURCE_STATES        StateAfter,
    UINT                         Subresource,
    D3D12_RESOURCE_BARRIER_FLAGS Flags)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = Flags;
    barrier.Transition.pResource   = pResource;
    barrier.Transition.StateBefore = StateBefore;
    barrier.Transition.StateAfter  = StateAfter;
    barrier.Transition.Subresource = Subresource;
    return barrier;
}