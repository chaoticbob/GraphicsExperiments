#include "window.h"

#include "dx_renderer.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr)) {                            \
            std::stringstream ss;                    \
            ss << "\n";                              \
            ss << "*** FUNCTION CALL FAILED *** \n"; \
            ss << "FUNCTION: " << #FN << "\n";       \
            ss << "\n";                              \
            GREX_LOG_ERROR(ss.str().c_str());        \
            assert(false);                           \
        }                                            \
    }

// =============================================================================
// Shader code
// =============================================================================
const char* gShaders = R"(

struct CameraProperties {
	float4x4 MVP;
};

ConstantBuffer<CameraProperties> Cam      : register(b0); // Constant buffer
Texture2D                        Tex0     : register(t1); // Texture
SamplerState                     Sampler0 : register(s2); // Sampler

struct VSOutput {
    float4 PositionCS : SV_POSITION;
    float2 TexCoord   : TEXCOORD;
};

VSOutput vsmain(float3 PositionOS : POSITION, float2 TexCoord : TEXCOORD)
{
    VSOutput output = (VSOutput)0;
    output.PositionCS = mul(Cam.MVP, float4(PositionOS, 1));
    output.TexCoord = TexCoord;
    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{
    float4 color = Tex0.Sample(Sampler0, input.TexCoord);
    return color;
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

static LPCWSTR gVSShaderName = L"vsmain";
static LPCWSTR gPSShaderName = L"psmain";

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateTexture(DxRenderer* pRenderer, ID3D12Resource** ppTexture);
void CreateDescriptorHeaps(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppCBVSRVUAVHeap,
    ID3D12DescriptorHeap** ppSamplerHeap);
void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppVertexBuffer,
    ID3D12Resource** ppTexCoordBuffer);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<DxRenderer> renderer = std::make_unique<DxRenderer>();

    if (!InitDx(renderer.get(), gEnableDebug)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<char> dxilVS;
    std::vector<char> dxilPS;
    {
        std::string errorMsg;
        HRESULT     hr = CompileHLSL(gShaders, "vsmain", "vs_6_0", &dxilVS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(gShaders, "psmain", "ps_6_0", &dxilPS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature> rootSig;
    CreateGlobalRootSig(renderer.get(), &rootSig);

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    ComPtr<ID3D12PipelineState> pipelineState;
    CHECK_CALL(CreateDrawTexturePipeline(
        renderer.get(),
        rootSig.Get(),
        dxilVS,
        dxilPS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipelineState));

    // *************************************************************************
    // Texture
    // *************************************************************************
    ComPtr<ID3D12Resource> texture;
    CreateTexture(renderer.get(), &texture);

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> cbvsrvuavHeap;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    CreateDescriptorHeaps(renderer.get(), &cbvsrvuavHeap, &samplerHeap);
    {
        // Write texture descriptor
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                          = texture->GetDesc().Format;
        srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip       = 0;
        srvDesc.Texture2D.MipLevels             = 1;
        srvDesc.Texture2D.PlaneSlice            = 0;
        srvDesc.Texture2D.ResourceMinLODClamp   = 0;

        renderer->Device->CreateShaderResourceView(texture.Get(), &srvDesc, cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart());

        // Write sampler descriptor
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter             = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU           = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV           = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW           = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MipLODBias         = D3D12_DEFAULT_MIP_LOD_BIAS;
        samplerDesc.MaxAnisotropy      = 0;
        samplerDesc.ComparisonFunc     = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samplerDesc.MinLOD             = 0;
        samplerDesc.MaxLOD             = 1;

        renderer->Device->CreateSampler(&samplerDesc, samplerHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> texCoordBuffer;
    CreateGeometryBuffers(renderer.get(), &indexBuffer, &positionBuffer, &texCoordBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "301_textured_cube_d3d12");
    if (!window) {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT)) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command allocator
    // *************************************************************************
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    {
        CHECK_CALL(renderer->Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,    // type
            IID_PPV_ARGS(&commandAllocator))); // ppCommandList
    }

    // *************************************************************************
    // Command list
    // *************************************************************************
    ComPtr<ID3D12GraphicsCommandList5> commandList;
    {
        CHECK_CALL(renderer->Device->CreateCommandList1(
            0,                              // nodeMask
            D3D12_COMMAND_LIST_TYPE_DIRECT, // type
            D3D12_COMMAND_LIST_FLAG_NONE,   // flags
            IID_PPV_ARGS(&commandList)));   // ppCommandList
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

        ComPtr<ID3D12Resource> swapchainBuffer;
        CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        ID3D12DescriptorHeap* pDescriptorHeaps[2] = {cbvsrvuavHeap.Get(), samplerHeap.Get()};
        commandList->SetDescriptorHeaps(2, pDescriptorHeaps);

        D3D12_RESOURCE_BARRIER preRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &preRenderBarrier);
        {
            commandList->OMSetRenderTargets(
                1,
                &renderer->SwapchainRTVDescriptorHandles[bufferIndex],
                false,
                &renderer->SwapchainDSVDescriptorHandles[bufferIndex]);

            float clearColor[4] = {0.23f, 0.23f, 0.31f, 0};
            commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 0, nullptr);
            commandList->ClearDepthStencilView(renderer->SwapchainDSVDescriptorHandles[bufferIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 0, nullptr);

            mat4 modelMat = glm::rotate(static_cast<float>(glfwGetTime()), vec3(0, 1, 0)) *
                            glm::rotate(static_cast<float>(glfwGetTime()), vec3(1, 0, 0));
            mat4 viewMat = glm::lookAt(vec3(0, 0, 2), vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
            mat4 mvpMat  = projMat * viewMat * modelMat;

            commandList->SetGraphicsRootSignature(rootSig.Get());
            commandList->SetGraphicsRoot32BitConstants(0, 16, &mvpMat, 0);

            commandList->SetGraphicsRootDescriptorTable(1, cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart());
            commandList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation          = indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes             = static_cast<UINT>(indexBuffer->GetDesc().Width);
            ibv.Format                  = DXGI_FORMAT_R32_UINT;
            commandList->IASetIndexBuffer(&ibv);

            D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
            vbvs[0].BufferLocation           = positionBuffer->GetGPUVirtualAddress();
            vbvs[0].SizeInBytes              = static_cast<UINT>(positionBuffer->GetDesc().Width);
            vbvs[0].StrideInBytes            = 12;
            vbvs[1].BufferLocation           = texCoordBuffer->GetGPUVirtualAddress();
            vbvs[1].SizeInBytes              = static_cast<UINT>(texCoordBuffer->GetDesc().Width);
            vbvs[1].StrideInBytes            = 8;

            commandList->IASetVertexBuffers(0, 2, vbvs);

            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

            commandList->SetPipelineState(pipelineState.Get());

            commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
        }
        D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &postRenderBarrier);

        commandList->Close();

        ID3D12CommandList* pList = commandList.Get();
        renderer->Queue->ExecuteCommandLists(1, &pList);

        if (!WaitForGpu(renderer.get())) {
            assert(false && "WaitForGpu failed");
            break;
        }

        // Present
        if (!SwapchainPresent(renderer.get())) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    D3D12_DESCRIPTOR_RANGE ranges[2]            = {};
    ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors                    = 1;
    ranges[0].BaseShaderRegister                = 1;
    ranges[0].RegisterSpace                     = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors                    = 1;
    ranges[1].BaseShaderRegister                = 2;
    ranges[1].RegisterSpace                     = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3]                = {};
    rootParameters[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.Num32BitValues            = 16;
    rootParameters[0].Constants.ShaderRegister            = 0;
    rootParameters[0].Constants.RegisterSpace             = 0;
    rootParameters[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges   = &ranges[0];
    rootParameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges   = &ranges[1];
    rootParameters[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 3;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT          hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) {
        std::string errorMsg = std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        assert(false && "D3D12SerializeRootSignature failed");
    }

    CHECK_CALL(pRenderer->Device->CreateRootSignature(
        0,                         // nodeMask
        blob->GetBufferPointer(),  // pBloblWithRootSignature
        blob->GetBufferSize(),     // blobLengthInBytes
        IID_PPV_ARGS(ppRootSig))); // riid, ppvRootSignature
}

void CreateTexture(DxRenderer* pRenderer, ID3D12Resource** ppTexture)
{
    auto bitmap = LoadImage8u(GetAssetPath("textures/brushed_metal.png"));
    assert((bitmap.GetSizeInBytes() > 0) && "image load failed");

    CHECK_CALL(CreateTexture(
        pRenderer,
        bitmap.GetWidth(),
        bitmap.GetHeight(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        bitmap.GetSizeInBytes(),
        bitmap.GetPixels(),
        ppTexture));
}

void CreateDescriptorHeaps(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppCBVSRVUAVHeap, ID3D12DescriptorHeap** ppSamplerHeap)
{
    //
    // CBVSRVUAV heap
    //
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors             = 1;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(
        &desc,
        IID_PPV_ARGS(ppCBVSRVUAVHeap)));

    //
    // Sampler heap
    //
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = 1;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(
        &desc,
        IID_PPV_ARGS(ppSamplerHeap)));
}

void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppTexCoordBuffer)
{
    TriMesh mesh = TriMesh::Cube(vec3(1), false, TriMesh::Options().EnableTexCoords());

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        ppIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        ppPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        ppTexCoordBuffer));
}