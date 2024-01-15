#pragma once

#include "config.h"

#if defined(GREX_USE_D3DX12)
#    include "directx/d3dx12.h"
#else
#    include <d3d12.h>
#endif

#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <dxcapi.h>

#define GREX_DEFAULT_RTV_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM
#define GREX_DEFAULT_DSV_FORMAT DXGI_FORMAT_D32_FLOAT

enum DxPipelineFlags
{
    DX_PIPELINE_FLAGS_INTERLEAVED_ATTRS = 0x00000001
};

struct DxRenderer
{
    bool                                     DebugEnabled                  = true;
    ComPtr<IDXGIFactory5>                    Factory                       = nullptr;
    ComPtr<IDXGIAdapter4>                    Adapter                       = nullptr;
    ComPtr<ID3D12Device7>                    Device                        = nullptr;
    ComPtr<ID3D12Fence>                      DeviceFence                   = nullptr;
    UINT                                     DeviceFenceValue              = 0;
    HANDLE                                   DeviceWaitEventHandle         = nullptr;
    ComPtr<ID3D12CommandQueue>               Queue                         = nullptr;
    ComPtr<IDXGISwapChain4>                  Swapchain                     = nullptr;
    UINT                                     SwapchainBufferCount          = 0;
    DXGI_FORMAT                              SwapchainRTVFormat            = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT                              SwapchainDSVFormat            = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D12Fence>                      SwapchainFence                = nullptr;
    UINT64                                   SwapchainFenceValue           = 0;
    HANDLE                                   SwapchainWaitEventHandle      = nullptr;
    ComPtr<ID3D12DescriptorHeap>             SwapchainRTVDescriptorHeap    = nullptr;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> SwapchainRTVDescriptorHandles = {};
    std::vector<ComPtr<ID3D12Resource>>      SwapchainDSVBuffers           = {};
    ComPtr<ID3D12DescriptorHeap>             SwapchainDSVDescriptorHeap    = nullptr;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> SwapchainDSVDescriptorHandles = {};
    ComPtr<ID3D12DescriptorHeap>             ImGuiFontDescriptorHeap       = nullptr;

    DxRenderer();
    ~DxRenderer();
};

bool InitDx(DxRenderer* pRenderer, bool enableDebug);
bool InitSwapchain(DxRenderer* pRenderer, HWND hwnd, uint32_t width, uint32_t height, uint32_t bufferCount = 2, DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN);
bool WaitForGpu(DxRenderer* pRenderer);
bool SwapchainPresent(DxRenderer* pRenderer);

DXGI_FORMAT ToDxFormat(GREXFormat format);

HRESULT CreateBuffer(DxRenderer* pRenderer, size_t size, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, ID3D12Resource* pSrcBuffer, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, size_t bufferSize, size_t srcSize, const void* pSrcData, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, D3D12_HEAP_TYPE heapType, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, size_t srcSize, const void* pSrcData, size_t minAlignment, ID3D12Resource** ppResource);
HRESULT CreateBuffer(DxRenderer* pRenderer, size_t rowStride, size_t totalNumRows, const void* pSrcData, ID3D12Resource** ppResource);
HRESULT CreateUAVBuffer(DxRenderer* pRenderer, size_t size, D3D12_RESOURCE_STATES initialResourceState, ID3D12Resource** ppResource);

HRESULT CreateTexture(
    DxRenderer*      pRenderer,
    uint32_t         width,
    uint32_t         height,
    DXGI_FORMAT      format,
    uint32_t         numMipLevels,
    uint32_t         numArrayLayers,
    ID3D12Resource** ppResource);

HRESULT CreateTexture(
    DxRenderer*                   pRenderer,
    uint32_t                      width,
    uint32_t                      height,
    DXGI_FORMAT                   format,
    const std::vector<MipOffset>& mipOffsets,
    uint64_t                      srcSizeBytes,
    const void*                   pSrcData,
    ID3D12Resource**              ppResource);

HRESULT CreateTexture(
    DxRenderer*      pRenderer,
    uint32_t         width,
    uint32_t         height,
    DXGI_FORMAT      format,
    uint64_t         srcSizeBytes,
    const void*      pSrcData,
    ID3D12Resource** ppResource);

void CreateDescriptoBufferSRV(
    DxRenderer*                 pRenderer,
    uint32_t                    firstElement,
    uint32_t                    numElements,
    uint32_t                    structureByteStride,
    ID3D12Resource*             pResource,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor);

void CreateDescriptorTexture2D(
    DxRenderer*                 pRenderer,
    ID3D12Resource*             pResource,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
    UINT                        MostDetailedMip = 0,
    UINT                        MipLevels       = 1,
    UINT                        PlaneSlice      = 0);

D3D12_RESOURCE_BARRIER CreateTransition(
    ID3D12Resource*              pResource,
    D3D12_RESOURCE_STATES        StateBefore,
    D3D12_RESOURCE_STATES        StateAfter,
    UINT                         Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
    D3D12_RESOURCE_BARRIER_FLAGS Flags       = D3D12_RESOURCE_BARRIER_FLAG_NONE);

HRESULT CreateDrawVertexColorPipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      vsShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    ID3D12PipelineState**         ppPipeline,
    D3D12_CULL_MODE               cullMode      = D3D12_CULL_MODE_BACK,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    uint32_t                      pipelineFlags = 0);

HRESULT CreateDrawVertexColorAndTexCoordPipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      vsShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    ID3D12PipelineState**         ppPipeline,
    D3D12_CULL_MODE               cullMode      = D3D12_CULL_MODE_BACK,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    uint32_t                      pipelineFlags = 0);

HRESULT CreateDrawNormalPipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    bool                     enableTangents = false,
    D3D12_CULL_MODE          cullMode       = D3D12_CULL_MODE_BACK);

HRESULT CreateDrawTexturePipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode = D3D12_CULL_MODE_BACK);

HRESULT CreateDrawBasicPipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode = D3D12_CULL_MODE_BACK);

HRESULT CreateGraphicsPipeline1(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode = D3D12_CULL_MODE_BACK);

HRESULT CreateGraphicsPipeline2(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& vsShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode = D3D12_CULL_MODE_BACK);

#if defined(GREX_USE_D3DX12)
HRESULT CreateMeshShaderPipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& asShaderBytecode,
    const std::vector<char>& msShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode = D3D12_CULL_MODE_BACK);

HRESULT CreateMeshShaderPipeline(
    DxRenderer*              pRenderer,
    ID3D12RootSignature*     pRootSig,
    const std::vector<char>& msShaderBytecode,
    const std::vector<char>& psShaderBytecode,
    DXGI_FORMAT              rtvFormat,
    DXGI_FORMAT              dsvFormat,
    ID3D12PipelineState**    ppPipeline,
    D3D12_CULL_MODE          cullMode = D3D12_CULL_MODE_BACK);
#endif // defined(GREX_USE_D3DX12)

HRESULT CompileHLSL(
    const std::string& shaderSource,
    const std::string& entryPoint, // Ignored if profile is lib_6_*
    const std::string& profile,
    std::vector<char>* pDXIL,
    std::string*       pErrorMsg);

HRESULT CopyDataToBuffer(size_t dataSize, void* pData, ID3D12Resource* pBuffer);