#include "dx_renderer.h"

bool     IsCompressed(DXGI_FORMAT fmt);
bool     IsVideo(DXGI_FORMAT fmt);
uint32_t BitsPerPixel(DXGI_FORMAT fmt);

uint32_t PixelStride(DXGI_FORMAT fmt)
{
    uint32_t nbytes = BitsPerPixel(fmt) / 8;
    return nbytes;
}

// =================================================================================================
// DxRenderer
// =================================================================================================
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

bool HasMeshShaderPipelineStats(DxRenderer* pRenderer)
{
    if (IsNull(pRenderer) || !pRenderer->Device)
    {
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS9 featureData = {};
    pRenderer->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &featureData, sizeof(featureData));
    return featureData.MeshShaderPipelineStatsSupported;
}

bool InitSwapchain(DxRenderer* pRenderer, HWND hwnd, uint32_t width, uint32_t height, uint32_t bufferCount, DXGI_FORMAT dsvFormat)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    pRenderer->SwapchainRTVFormat = GREX_DEFAULT_RTV_FORMAT;
    pRenderer->SwapchainDSVFormat = GREX_DEFAULT_DSV_FORMAT;

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

    // Create RTV stuff
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

    // Create DSV stuff
    if (pRenderer->SwapchainDSVFormat != DXGI_FORMAT_UNKNOWN) {
        // Buffers
        for (UINT i = 0; i < pRenderer->SwapchainBufferCount; ++i) {
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type                  = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC dsvBufferDesc = {};
            dsvBufferDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            dsvBufferDesc.Alignment           = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            dsvBufferDesc.Width               = width;
            dsvBufferDesc.Height              = height;
            dsvBufferDesc.DepthOrArraySize    = 1;
            dsvBufferDesc.MipLevels           = 1;
            dsvBufferDesc.Format              = pRenderer->SwapchainDSVFormat;
            dsvBufferDesc.SampleDesc          = {1, 0};
            dsvBufferDesc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            dsvBufferDesc.Flags               = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE optimizedClearValue    = {};
            optimizedClearValue.DepthStencil.Depth   = 1.0f;
            optimizedClearValue.DepthStencil.Stencil = 0xFF;
            optimizedClearValue.Format               = pRenderer->SwapchainDSVFormat;

            ComPtr<ID3D12Resource> dsvBuffer;
            HRESULT                hr = pRenderer->Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &dsvBufferDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &optimizedClearValue,
                IID_PPV_ARGS(&dsvBuffer));
            if (FAILED(hr)) {
                assert(false && "ID3D12Device::CreateCommittedResource (DSV buffer) failed");
                return false;
            }

            pRenderer->SwapchainDSVBuffers.push_back(dsvBuffer);
        }

        // Descriptors
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            desc.NumDescriptors             = pRenderer->SwapchainBufferCount;
            desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            hr = pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pRenderer->SwapchainDSVDescriptorHeap));
            if (FAILED(hr)) {
                assert(false && "ID3D12Device::CreateDescriptorHeap failed");
                return false;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE dsv = pRenderer->SwapchainDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < pRenderer->SwapchainBufferCount; ++i) {
                ID3D12Resource* pDSVBuffer = pRenderer->SwapchainDSVBuffers[i].Get();

                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
                dsvDesc.Format                        = pRenderer->SwapchainDSVFormat;
                dsvDesc.ViewDimension                 = D3D12_DSV_DIMENSION_TEXTURE2D;

                pRenderer->Device->CreateDepthStencilView(pDSVBuffer, &dsvDesc, dsv);
                pRenderer->SwapchainDSVDescriptorHandles.push_back(dsv);

                dsv.ptr += pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            }
        }
    }

    // Create fence
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

DXGI_FORMAT ToDxFormat(GREXFormat format)
{
    // clang-format off
    switch (format) {
        default: break;
        case GREX_FORMAT_R8_UNORM           : return DXGI_FORMAT_R8_UNORM;
        case GREX_FORMAT_R8G8_UNORM         : return DXGI_FORMAT_R8G8_UNORM;
        case GREX_FORMAT_R8G8B8A8_UNORM     : return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GREX_FORMAT_R8_UINT            : return DXGI_FORMAT_R8_UINT;
        case GREX_FORMAT_R16_UINT           : return DXGI_FORMAT_R16_UINT;
        case GREX_FORMAT_R32_UINT           : return DXGI_FORMAT_R32_UINT;
        case GREX_FORMAT_R32_FLOAT          : return DXGI_FORMAT_R32_FLOAT;
        case GREX_FORMAT_R32G32_FLOAT       : return DXGI_FORMAT_R32G32_FLOAT;
        case GREX_FORMAT_R32G32B32A32_FLOAT : return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case GREX_FORMAT_BC1_RGB            : return DXGI_FORMAT_UNKNOWN; // Does D3D12 support BC1 RGB?
        case GREX_FORMAT_BC3_RGBA           : return DXGI_FORMAT_BC3_UNORM; 
        case GREX_FORMAT_BC4_R              : return DXGI_FORMAT_BC4_UNORM;
        case GREX_FORMAT_BC5_RG             : return DXGI_FORMAT_BC5_UNORM;
        case GREX_FORMAT_BC6H_SFLOAT        : return DXGI_FORMAT_BC6H_SF16;
        case GREX_FORMAT_BC6H_UFLOAT        : return DXGI_FORMAT_BC6H_UF16;
        case GREX_FORMAT_BC7_RGBA           : return DXGI_FORMAT_BC7_UNORM;
    }
    // clang-format on
    return DXGI_FORMAT_UNKNOWN;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t size, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource)
{
    if (IsNull(pRenderer)) {
        return E_UNEXPECTED;
    }
    if (IsNull(ppResource)) {
        return E_UNEXPECTED;
    }

    switch (heapType) {
        default: return E_INVALIDARG;
        case D3D12_HEAP_TYPE_DEFAULT:
        case D3D12_HEAP_TYPE_UPLOAD:
        case D3D12_HEAP_TYPE_READBACK:
            break;
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
    desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = heapType;

    D3D12_RESOURCE_STATES initialResourceState = (heapType == D3D12_HEAP_TYPE_UPLOAD) ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

    HRESULT hr = pRenderer->Device->CreateCommittedResource(
        &heapProperties,             // pHeapProperties
        D3D12_HEAP_FLAG_NONE,        // HeapFlags
        &desc,                       // pDesc
        initialResourceState,        // InitialResourceState
        nullptr,                     // pOptimizedClearValues
        IID_PPV_ARGS(ppResource));   // riidResource, ppvResouce
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, ID3D12Resource* pSrcBuffer, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource)
{
    if (IsNull(pSrcBuffer)) {
        return E_UNEXPECTED;
    }

    UINT64 srcSize = pSrcBuffer->GetDesc().Width;
    HRESULT hr = CreateBuffer(pRenderer, static_cast<uint32_t>(srcSize), heapType, ppResource);
    if (FAILED(hr)) {
        return hr;
    }

    // Copy
    {
        ComPtr<ID3D12CommandAllocator> cmdAllocator;
        hr = pRenderer->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));
        if (FAILED(hr)) {
            return hr;
        }

        ComPtr<ID3D12GraphicsCommandList> cmdList;
        hr = pRenderer->Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
        if (FAILED(hr)) {
            return hr;
        }

        hr = cmdAllocator->Reset();
        if (FAILED(hr)) {
            return hr;
        }

        hr = cmdList->Reset(cmdAllocator.Get(), nullptr);
        if (FAILED(hr)) {
            return hr;
        }

        // Build command buffer
        {
            cmdList->CopyBufferRegion(
                (*ppResource), // pDstBuffer
                0,             // DstOffset
                pSrcBuffer,    // pSrcBuffer
                0,             // SrcOffset
                srcSize);      // NumBytes
        }
        hr = cmdList->Close();
        if (FAILED(hr)) {
            return hr;
        } 

        ID3D12CommandList* pList = cmdList.Get();
        pRenderer->Queue->ExecuteCommandLists(1, &pList);

        if (!WaitForGpu(pRenderer)) {
            return E_FAIL;
        }    
    }

    return S_OK;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t bufferSize, size_t srcSize, const void* pSrcData, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource)
{
    if (srcSize > bufferSize) {
        return E_INVALIDARG;
    }

    HRESULT hr = CreateBuffer(pRenderer, bufferSize, heapType, ppResource);
    if (FAILED(hr)) {
        return hr;
    }

    if (!IsNull(pSrcData)) {
        // Target buffer pointer - assume output resource
        ID3D12Resource* pTargetBuffer = (*ppResource);

        // Create a staging buffer if heap type is DEFAULT and reassign
        // the target buffer pointer
        //
        ComPtr<ID3D12Resource> stagingBuffer;
        if (heapType == D3D12_HEAP_TYPE_DEFAULT) {
            HRESULT hr = CreateBuffer(pRenderer, srcSize, D3D12_HEAP_TYPE_UPLOAD, &stagingBuffer);
            if (FAILED(hr)) {
                return hr;
            }

            pTargetBuffer = stagingBuffer.Get();
        }

        // Copy source data to target buffer
        {
            void* pData = nullptr;
            hr          = pTargetBuffer->Map(0, nullptr, &pData);
            if (FAILED(hr)) {
                return hr;
            }

            memcpy(pData, pSrcData, srcSize);

            pTargetBuffer->Unmap(0, nullptr);
        }

        // Copy from pTargetBuffer to ppResource if heap type is DEFAULT
        if (heapType == D3D12_HEAP_TYPE_DEFAULT) {
            ComPtr<ID3D12CommandAllocator> cmdAllocator;
            hr = pRenderer->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));
            if (FAILED(hr)) {
                return hr;
            }

            ComPtr<ID3D12GraphicsCommandList> cmdList;
            hr = pRenderer->Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
            if (FAILED(hr)) {
                return hr;
            }

            hr = cmdAllocator->Reset();
            if (FAILED(hr)) {
                return hr;
            }

            hr = cmdList->Reset(cmdAllocator.Get(), nullptr);
            if (FAILED(hr)) {
                return hr;
            }

            // Build command buffer
            {
                cmdList->CopyBufferRegion(
                    (*ppResource),                 // pDstBuffer
                    0,                             // DstOffset
                    pTargetBuffer,                 // pSrcBuffer
                    0,                             // SrcOffset
                    static_cast<UINT64>(srcSize)); // NumBytes
            }
            hr = cmdList->Close();
            if (FAILED(hr)) {
                return hr;
            }

            ID3D12CommandList* pList = cmdList.Get();
            pRenderer->Queue->ExecuteCommandLists(1, &pList);

            if (!WaitForGpu(pRenderer)) {
                return E_FAIL;
            }
        }
    }

    return S_OK;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource)
{
    const size_t bufferSize = srcSize;

    HRESULT hr = CreateBuffer(pRenderer, bufferSize, srcSize, pSrcData, heapType, ppResource);
    if (FAILED(hr)) {
        return hr;
    }
    return S_OK;
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, ID3D12Resource** ppResource)
{
    HRESULT hr = CreateBuffer(pRenderer, srcSize, pSrcData, D3D12_HEAP_TYPE_UPLOAD, ppResource);
    if (FAILED(hr)) {
        return hr;
    }
    return S_OK;

    /*
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
    */
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, size_t minAlignment, ID3D12Resource** ppResource)
{
    if (minAlignment > 0) {
        srcSize = Align<size_t>(srcSize, minAlignment);
    }

    return CreateBuffer(pRenderer, srcSize, pSrcData, ppResource);
}

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t rowStride, size_t numRows, const void* pSrcData, ID3D12Resource** ppResource)
{
    size_t alignedRowStride  = Align<size_t>(rowStride, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    size_t alignedBufferSize = alignedRowStride * numRows;

    HRESULT hr = CreateBuffer(pRenderer, alignedBufferSize, nullptr, ppResource);
    if (FAILED(hr)) {
        return hr;
    }

    char* pDstRow = nullptr;
    hr            = (*ppResource)->Map(0, nullptr, reinterpret_cast<void**>(&pDstRow));
    if (FAILED(hr)) {
        return hr;
    }

    const char* pSrcRow = static_cast<const char*>(pSrcData);
    for (size_t i = 0; i < numRows; ++i) {
        memcpy(pDstRow, pSrcRow, rowStride);
        pSrcRow += rowStride;
        pDstRow += alignedRowStride;
    }

    (*ppResource)->Unmap(0, nullptr);

    return S_OK;
}

HRESULT CreateUAVBuffer(DxRenderer* pRenderer, size_t size, D3D12_RESOURCE_STATES initialResourceState, ID3D12Resource** ppResource)
{
    if (IsNull(pRenderer)) {
        return E_UNEXPECTED;
    }
    if (IsNull(ppResource)) {
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

HRESULT CreateTexture(
    DxRenderer*      pRenderer,
    uint32_t         width,
    uint32_t         height,
    DXGI_FORMAT      format,
    uint32_t         numMipLevels,
    uint32_t         numArrayLayers,
    ID3D12Resource** ppResource)
{
    if (IsNull(pRenderer)) {
        return E_UNEXPECTED;
    }
    if (IsNull(ppResource)) {
        return E_UNEXPECTED;
    }
    if ((format == DXGI_FORMAT_UNKNOWN) || IsVideo(format)) {
        return E_INVALIDARG;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment           = 0;
    desc.Width               = static_cast<UINT64>(width);
    desc.Height              = static_cast<UINT>(height);
    desc.DepthOrArraySize    = numArrayLayers;
    desc.MipLevels           = numMipLevels;
    desc.Format              = format;
    desc.SampleDesc          = {1, 0};
    desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = pRenderer->Device->CreateCommittedResource(
        &heapProperties,                // pHeapProperties
        D3D12_HEAP_FLAG_NONE,           // HeapFlags
        &desc,                          // pDesc
        D3D12_RESOURCE_STATE_COPY_DEST, // InitialResourceState
        nullptr,                        // pOptimizedClearValues
        IID_PPV_ARGS(ppResource));      // riidResource, ppvResouce
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateTexture(
    DxRenderer*                   pRenderer,
    uint32_t                      width,
    uint32_t                      height,
    DXGI_FORMAT                   format,
    const std::vector<MipOffset>& mipOffsets,
    uint64_t                      srcSizeBytes,
    const void*                   pSrcData,
    ID3D12Resource**              ppResource)
{
    uint32_t numMipLevels = static_cast<uint32_t>(mipOffsets.size());

    HRESULT hr = CreateTexture(
        pRenderer,
        width,
        height,
        format,
        numMipLevels,
        1, // numArrayLayers
        ppResource);
    if (FAILED(hr)) {
        return hr;
    }

    if (!IsNull(pSrcData)) {
        ComPtr<ID3D12Resource> stagingBuffer;
        if (IsCompressed(format)) {
            hr = CreateBuffer(pRenderer, srcSizeBytes, pSrcData, &stagingBuffer);
            if (FAILED(hr)) {
                assert(false && "create staging buffer failed");
                return hr;
            }
        }
        else {
            const uint32_t rowStride = width * PixelStride(format);
            // Calculate the total number of rows for all mip maps
            uint32_t numRows = 0;
            {
                uint32_t mipHeight = height;
                for (UINT level = 0; level < numMipLevels; ++level) {
                    numRows += mipHeight;
                    mipHeight >>= 1;
                }
            }

            hr = CreateBuffer(pRenderer, rowStride, numRows, pSrcData, &stagingBuffer);
            if (FAILED(hr)) {
                assert(false && "create staging buffer failed");
                return hr;
            }
        }

        ComPtr<ID3D12CommandAllocator> cmdAllocator;
        hr = pRenderer->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));
        if (FAILED(hr)) {
            assert(false && "create staging command allocator failed");
            return hr;
        }

        ComPtr<ID3D12GraphicsCommandList> cmdList;
        hr = pRenderer->Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
        if (FAILED(hr)) {
            assert(false && "create staging command list failed");
            return hr;
        }

        hr = cmdAllocator->Reset();
        if (FAILED(hr)) {
            assert(false && "reset command allocator failed");
            return hr;
        }

        hr = cmdList->Reset(cmdAllocator.Get(), nullptr);
        if (FAILED(hr)) {
            assert(false && "reset command list failed");
            return hr;
        }

        // Build command buffer
        {
            uint32_t levelWidth  = width;
            uint32_t levelHeight = height;
            for (UINT level = 0; level < numMipLevels; ++level) {
                const auto&    mipOffset    = mipOffsets[level];
                uint32_t mipRowStride = Align<uint32_t>(mipOffset.RowStride, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
                if (IsCompressed(format)) {
                    //
                    // This is hack based on values returned by GetCopyableFootprints().
                    //
                    mipRowStride = levelWidth * 4;
                }

                D3D12_TEXTURE_COPY_LOCATION dst = {};
                dst.pResource                   = *ppResource;
                dst.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex            = level;

                D3D12_TEXTURE_COPY_LOCATION src        = {};
                src.pResource                          = stagingBuffer.Get();
                src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset             = mipOffset.Offset;
                src.PlacedFootprint.Footprint.Format   = format;
                src.PlacedFootprint.Footprint.Width    = static_cast<UINT>(levelWidth);
                src.PlacedFootprint.Footprint.Height   = static_cast<UINT>(levelHeight);
                src.PlacedFootprint.Footprint.Depth    = 1;
                src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(mipRowStride);

                cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource   = *ppResource;
                barrier.Transition.Subresource = level;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

                cmdList->ResourceBarrier(1, &barrier);

                levelWidth >>= 1;
                levelHeight >>= 1;
            }
        }
        hr = cmdList->Close();
        if (FAILED(hr)) {
            assert(false && "close command list failed");
            return hr;
        }

        ID3D12CommandList* pList = cmdList.Get();
        pRenderer->Queue->ExecuteCommandLists(1, &pList);

        if (!WaitForGpu(pRenderer)) {
            assert(false && "WaitForGpu failed");
            return false;
        }
    }

    return S_OK;
}

HRESULT CreateTexture(
    DxRenderer*      pRenderer,
    uint32_t         width,
    uint32_t         height,
    DXGI_FORMAT      format,
    uint64_t         srcSizeBytes,
    const void*      pSrcData,
    ID3D12Resource** ppResource)
{
    MipOffset mipOffset = {};
    mipOffset.Offset    = 0;
    mipOffset.RowStride = width * PixelStride(format);

    return CreateTexture(
        pRenderer,
        width,
        height,
        format,
        {mipOffset},
        srcSizeBytes,
        pSrcData,
        ppResource);
}

void CreateDescriptoBufferSRV(
    DxRenderer*                 pRenderer,
    uint32_t                    firstElement,
    uint32_t                    numElements,
    uint32_t                    structureByteStride,
    ID3D12Resource*             pResource,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format                          = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement             = firstElement;
    desc.Buffer.NumElements              = numElements;
    desc.Buffer.StructureByteStride      = structureByteStride;
    desc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_NONE;

    pRenderer->Device->CreateShaderResourceView(pResource, &desc, descriptor);
}

void CreateDescriptorTexture2D(
    DxRenderer*                 pRenderer,
    ID3D12Resource*             pResource,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
    UINT                        MostDetailedMip,
    UINT                        MipLevels,
    UINT                        PlaneSlice)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format                          = pResource->GetDesc().Format;
    desc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MostDetailedMip       = MostDetailedMip;
    desc.Texture2D.MipLevels             = MipLevels;
    desc.Texture2D.PlaneSlice            = PlaneSlice;
    desc.Texture2D.ResourceMinLODClamp   = 0;

    pRenderer->Device->CreateShaderResourceView(pResource, &desc, descriptor);
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

HRESULT CreateDrawVertexColorPipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      vsShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    ID3D12PipelineState**         ppPipeline,
    D3D12_CULL_MODE               cullMode,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType,
    uint32_t                      pipelineFlags)
{
    bool                     isInterleavedAttrs  = pipelineFlags & DX_PIPELINE_FLAGS_INTERLEAVED_ATTRS;
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[2] = {};
    inputElementDesc[0].SemanticName             = "POSITION";
    inputElementDesc[0].SemanticIndex            = 0;
    inputElementDesc[0].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot                = 0;
    inputElementDesc[0].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate     = 0;
    inputElementDesc[1].SemanticName             = "COLOR";
    inputElementDesc[1].SemanticIndex            = 0;
    inputElementDesc[1].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[1].InputSlot                = isInterleavedAttrs ? 0 : 1;
    inputElementDesc[1].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate     = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = 2;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = topologyType;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateDrawVertexColorAndTexCoordPipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      vsShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    ID3D12PipelineState**         ppPipeline,
    D3D12_CULL_MODE               cullMode,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType,
    uint32_t                      pipelineFlags)
{
    bool                     isInterleavedAttrs  = pipelineFlags & DX_PIPELINE_FLAGS_INTERLEAVED_ATTRS;
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[3] = {};
    inputElementDesc[0].SemanticName             = "POSITION";
    inputElementDesc[0].SemanticIndex            = 0;
    inputElementDesc[0].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot                = 0;
    inputElementDesc[0].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate     = 0;
    inputElementDesc[1].SemanticName             = "COLOR";
    inputElementDesc[1].SemanticIndex            = 0;
    inputElementDesc[1].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[1].InputSlot                = isInterleavedAttrs ? 0 : 1;
    inputElementDesc[1].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate     = 0;
    inputElementDesc[2].SemanticName             = "TEXCOORD";
    inputElementDesc[2].SemanticIndex            = 0;
    inputElementDesc[2].Format                   = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDesc[2].InputSlot                = isInterleavedAttrs ? 0 : 2;
    inputElementDesc[2].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[2].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[2].InstanceDataStepRate     = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = TRUE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = 3;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = topologyType;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateDrawNormalPipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    bool                     enableTangents,
    D3D12_CULL_MODE          cullMode)
{
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[4] = {};
    inputElementDesc[0].SemanticName             = "POSITION";
    inputElementDesc[0].SemanticIndex            = 0;
    inputElementDesc[0].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot                = 0;
    inputElementDesc[0].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate     = 0;
    inputElementDesc[1].SemanticName             = "NORMAL";
    inputElementDesc[1].SemanticIndex            = 0;
    inputElementDesc[1].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[1].InputSlot                = 1;
    inputElementDesc[1].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate     = 0;
    if (enableTangents) {
        inputElementDesc[2].SemanticName         = "TANGENT";
        inputElementDesc[2].SemanticIndex        = 0;
        inputElementDesc[2].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
        inputElementDesc[2].InputSlot            = 2;
        inputElementDesc[2].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
        inputElementDesc[2].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        inputElementDesc[2].InstanceDataStepRate = 0;
        inputElementDesc[3].SemanticName         = "BITANGENT";
        inputElementDesc[3].SemanticIndex        = 0;
        inputElementDesc[3].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
        inputElementDesc[3].InputSlot            = 3;
        inputElementDesc[3].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
        inputElementDesc[3].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        inputElementDesc[3].InstanceDataStepRate = 0;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = enableTangents ? 4 : 2;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateDrawTexturePipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode)
{
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[2] = {};
    inputElementDesc[0].SemanticName             = "POSITION";
    inputElementDesc[0].SemanticIndex            = 0;
    inputElementDesc[0].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot                = 0;
    inputElementDesc[0].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate     = 0;
    inputElementDesc[1].SemanticName             = "TEXCOORD";
    inputElementDesc[1].SemanticIndex            = 0;
    inputElementDesc[1].Format                   = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDesc[1].InputSlot                = 1;
    inputElementDesc[1].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate     = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = 2;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateDrawBasicPipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode)
{
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[3] = {};
    inputElementDesc[0].SemanticName             = "POSITION";
    inputElementDesc[0].SemanticIndex            = 0;
    inputElementDesc[0].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot                = 0;
    inputElementDesc[0].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate     = 0;
    inputElementDesc[1].SemanticName             = "TEXCOORD";
    inputElementDesc[1].SemanticIndex            = 0;
    inputElementDesc[1].Format                   = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDesc[1].InputSlot                = 1;
    inputElementDesc[1].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate     = 0;
    inputElementDesc[2].SemanticName             = "NORMAL";
    inputElementDesc[2].SemanticIndex            = 0;
    inputElementDesc[2].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[2].InputSlot                = 2;
    inputElementDesc[2].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[2].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[2].InstanceDataStepRate     = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = 3;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateGraphicsPipeline1(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode)
{
    const uint32_t kNumInputElements = 5;

    D3D12_INPUT_ELEMENT_DESC inputElementDesc[kNumInputElements] = {};
    // Position
    inputElementDesc[0].SemanticName         = "POSITION";
    inputElementDesc[0].SemanticIndex        = 0;
    inputElementDesc[0].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot            = 0;
    inputElementDesc[0].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate = 0;
    // TexCoord
    inputElementDesc[1].SemanticName         = "TEXCOORD";
    inputElementDesc[1].SemanticIndex        = 0;
    inputElementDesc[1].Format               = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDesc[1].InputSlot            = 1;
    inputElementDesc[1].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate = 0;
    // Normal
    inputElementDesc[2].SemanticName         = "NORMAL";
    inputElementDesc[2].SemanticIndex        = 0;
    inputElementDesc[2].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[2].InputSlot            = 2;
    inputElementDesc[2].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[2].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[2].InstanceDataStepRate = 0;
    // Tangent
    inputElementDesc[3].SemanticName         = "TANGENT";
    inputElementDesc[3].SemanticIndex        = 0;
    inputElementDesc[3].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[3].InputSlot            = 3;
    inputElementDesc[3].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[3].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[3].InstanceDataStepRate = 0;
    // Bitangent
    inputElementDesc[4].SemanticName         = "BITANGENT";
    inputElementDesc[4].SemanticIndex        = 0;
    inputElementDesc[4].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[4].InputSlot            = 4;
    inputElementDesc[4].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[4].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[4].InstanceDataStepRate = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = kNumInputElements;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

HRESULT CreateGraphicsPipeline2(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode)
{
    const uint32_t kNumInputElements = 4;

    D3D12_INPUT_ELEMENT_DESC inputElementDesc[kNumInputElements] = {};
    // Position
    inputElementDesc[0].SemanticName         = "POSITION";
    inputElementDesc[0].SemanticIndex        = 0;
    inputElementDesc[0].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot            = 0;
    inputElementDesc[0].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate = 0;
    // TexCoord
    inputElementDesc[1].SemanticName         = "TEXCOORD";
    inputElementDesc[1].SemanticIndex        = 0;
    inputElementDesc[1].Format               = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDesc[1].InputSlot            = 1;
    inputElementDesc[1].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate = 0;
    // Normal
    inputElementDesc[2].SemanticName         = "NORMAL";
    inputElementDesc[2].SemanticIndex        = 0;
    inputElementDesc[2].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[2].InputSlot            = 2;
    inputElementDesc[2].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[2].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[2].InstanceDataStepRate = 0;
    // Tangent
    inputElementDesc[3].SemanticName         = "TANGENT";
    inputElementDesc[3].SemanticIndex        = 0;
    inputElementDesc[3].Format               = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDesc[3].InputSlot            = 3;
    inputElementDesc[3].AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[3].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[3].InstanceDataStepRate = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = (dsvFormat != DXGI_FORMAT_UNKNOWN);
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = kNumInputElements;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

#if defined(GREX_USE_D3DX12)
HRESULT CreateMeshShaderPipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      asShaderBytecode,
    const std::vector<char>&      msShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    ID3D12PipelineState**         ppPipeline,
    D3D12_CULL_MODE               cullMode)
{
    D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc           = {};
    psoDesc.pRootSignature                                   = pRootSig;
    psoDesc.AS                                               = {asShaderBytecode.data(), asShaderBytecode.size()};
    psoDesc.MS                                               = {msShaderBytecode.data(), msShaderBytecode.size()};
    psoDesc.PS                                               = {psShaderBytecode.data(), psShaderBytecode.size()};
    psoDesc.BlendState.AlphaToCoverageEnable                 = FALSE;
    psoDesc.BlendState.IndependentBlendEnable                = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    psoDesc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode                         = cullMode;
    psoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
    psoDesc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable                  = FALSE;
    psoDesc.RasterizerState.MultisampleEnable                = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable            = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount                = 0;
    psoDesc.DepthStencilState.DepthEnable                    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable                  = FALSE;
    psoDesc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    psoDesc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    psoDesc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    psoDesc.DepthStencilState.BackFace                       = psoDesc.DepthStencilState.FrontFace;
    psoDesc.NumRenderTargets                                 = 1;
    psoDesc.RTVFormats[0]                                    = rtvFormat;
    psoDesc.DSVFormat                                        = dsvFormat;
    psoDesc.SampleDesc.Count                                 = 1;

    // This required unless you want to come up with own struct that handles
    // the stream requirements:
    //    https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html#createpipelinestate
    //
    CD3DX12_PIPELINE_MESH_STATE_STREAM psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

    D3D12_PIPELINE_STATE_STREAM_DESC steamDesc = {};
    steamDesc.SizeInBytes                      = sizeof(psoStream);
    steamDesc.pPipelineStateSubobjectStream    = &psoStream;

    HRESULT hr = pRenderer->Device->CreatePipelineState(&steamDesc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK; 
}

HRESULT CreateMeshShaderPipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      msShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    ID3D12PipelineState**         ppPipeline,
    D3D12_CULL_MODE               cullMode)
{
    return CreateMeshShaderPipeline(
        pRenderer,
        pRootSig,
        {},
        msShaderBytecode,
        psShaderBytecode,
        rtvFormat,
        dsvFormat,
        ppPipeline,
        cullMode);
}
#endif // defined(GREX_USE_D3DX12)

static std::wstring AsciiToUTF16(const std::string& ascii)
{
    std::wstring utf16;
    for (auto& c : ascii) {
        utf16.push_back(static_cast<std::wstring::value_type>(c));
    }
    return utf16;
}

HRESULT CompileHLSL(
    const std::string& shaderSource,
    const std::string& entryPoint,
    const std::string& profile,
    std::vector<char>* pDXIL,
    std::string*       pErrorMsg)
{
    // Check source
    if (shaderSource.empty()) {
        assert(false && "no shader source");
        return E_INVALIDARG;
    }
    // Check entry point
    if (entryPoint.empty() && (!profile.starts_with("lib_6_"))) {
        assert(false && "no entrypoint");
        return E_INVALIDARG;
    }
    // Check profile
    if (profile.empty()) {
        assert(false && "no profile");
        return E_INVALIDARG;
    }
    // Check output
    if (IsNull(pDXIL)) {
        assert(false && "DXIL output arg is null");
        return E_INVALIDARG;
    }

    ComPtr<IDxcLibrary> dxcLibrary;
    HRESULT             hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&dxcLibrary));

    ComPtr<IDxcCompiler3> dxcCompiler;
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));

    DxcBuffer source = {};
    source.Ptr       = shaderSource.data();
    source.Size      = shaderSource.length();

    std::wstring entryPointUTF16 = AsciiToUTF16(entryPoint);
    std::wstring profileUT16     = AsciiToUTF16(profile);

    std::vector<LPCWSTR> args;

    args.push_back(L"-E");
    args.push_back(entryPointUTF16.c_str());
    args.push_back(L"-T");
    args.push_back(profileUT16.c_str());

    // LPCWSTR args[2] = {L"-T", L"lib_6_3"};

    ComPtr<IDxcResult> result;
    hr = dxcCompiler->Compile(
        &source,
        args.data(),
        static_cast<UINT>(args.size()),
        nullptr,
        IID_PPV_ARGS(&result));
    if (FAILED(hr)) {
        assert(false && "compile failed");
        return hr;
    }

    ComPtr<IDxcBlob> errors;
    hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (FAILED(hr)) {
        assert(false && "Get error output failed");
        return hr;
    }
    if (errors && (errors->GetBufferSize() > 0) && !IsNull(pErrorMsg)) {
        const char* pBuffer    = static_cast<const char*>(errors->GetBufferPointer());
        size_t      bufferSize = static_cast<size_t>(errors->GetBufferSize());
        *pErrorMsg             = std::string(pBuffer, pBuffer + bufferSize);
        // std::string       errorMsg = std::string(reinterpret_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        // std::stringstream ss;
        // ss << "\n"
        //    << "Shader compiler error: " << errorMsg << "\n";
        // GREX_LOG_ERROR(ss.str().c_str());
        return E_FAIL;
    }

    ComPtr<IDxcBlob> shaderBinary;
    hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBinary), nullptr);
    if (FAILED(hr)) {
        assert(false && "Get compile output failed");
        return hr;
    }

    const char* pBuffer    = static_cast<const char*>(shaderBinary->GetBufferPointer());
    size_t      bufferSize = static_cast<size_t>(shaderBinary->GetBufferSize());
    *pDXIL                 = std::vector<char>(pBuffer, pBuffer + bufferSize);

    return S_OK;
}

//
// From: https://github.com/microsoft/DirectXTex/blob/main/DirectXTex/DirectXTex.inl#L53
//
bool IsCompressed(DXGI_FORMAT fmt) 
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;

    default:
        return false;
    }
}

//
// From: https://github.com/microsoft/DirectXTex/blob/main/DirectXTex/DirectXTexUtil.cpp#L366
//
bool IsVideo(DXGI_FORMAT fmt)
{
    switch (static_cast<int>(fmt)) {
        case DXGI_FORMAT_AYUV:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_YUY2:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
        case DXGI_FORMAT_NV11:
            // These video formats can be used with the 3D pipeline through special view mappings

        case DXGI_FORMAT_420_OPAQUE:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
        case DXGI_FORMAT_A8P8:
            // These are limited use video formats not usable in any way by the 3D pipeline

            return true;

        default:
            return false;
    }
}

//
// From: https://github.com/microsoft/DirectXTex
//
uint32_t BitsPerPixel(DXGI_FORMAT fmt)
{
    switch (static_cast<int>(fmt)) {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 128;

        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return 96;

        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            return 64;

        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_AYUV:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_YUY2:
            return 32;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            return 24;

        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
        case DXGI_FORMAT_A8P8:
        case DXGI_FORMAT_B4G4R4A4_UNORM:
            return 16;

        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_420_OPAQUE:
        case DXGI_FORMAT_NV11:
            return 12;

        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
            return 8;

        case DXGI_FORMAT_R1_UNORM:
            return 1;

        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            return 4;

        default:
            return 0;
    }
}

HRESULT CopyDataToBuffer(size_t dataSize, void* pData, ID3D12Resource* pBuffer)
{
    if ((dataSize == 0) || (pData == nullptr) || (pBuffer == nullptr)) {
        return E_INVALIDARG;
    }

    if (dataSize > pBuffer->GetDesc().Width) {
        assert(false && "data size exceeds buffer size");
        return E_ABORT;
    }

    char*   pDst = nullptr;
    HRESULT hr   = pBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pDst));
    if (FAILED(hr)) {
        return hr;
    }

    memcpy(pDst, pData, dataSize);

    pBuffer->Unmap(0, nullptr);

    return S_OK;
}