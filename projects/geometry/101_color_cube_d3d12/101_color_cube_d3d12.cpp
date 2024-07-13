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

ConstantBuffer<CameraProperties> Cam : register(b0); // Constant buffer

struct VSOutput {
    float4 PositionCS : SV_POSITION;
    float3 Color      : COLOR;
};

VSOutput vsmain(float3 PositionOS : POSITION, float3 Color : COLOR0)
{
    VSOutput output = (VSOutput)0;
    output.PositionCS = mul(Cam.MVP, float4(PositionOS, 1));
    output.Color = Color;
    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{
    return float4(input.Color, 1);   
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
void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppVertexBuffer,
    ID3D12Resource** ppVertexColorBuffer);

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
    CHECK_CALL(CreateDrawVertexColorPipeline(
        renderer.get(),
        rootSig.Get(),
        dxilVS,
        dxilPS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipelineState));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> vertexColorBuffer;
    CreateGeometryBuffers(renderer.get(), &indexBuffer, &positionBuffer, &vertexColorBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "101_color_cube_d3d12");
    if (!window) {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT)) {
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

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation          = indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes             = static_cast<UINT>(indexBuffer->GetDesc().Width);
            ibv.Format                  = DXGI_FORMAT_R32_UINT;
            commandList->IASetIndexBuffer(&ibv);

            D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
            vbvs[0].BufferLocation           = positionBuffer->GetGPUVirtualAddress();
            vbvs[0].SizeInBytes              = static_cast<UINT>(positionBuffer->GetDesc().Width);
            vbvs[0].StrideInBytes            = 12;
            vbvs[1].BufferLocation           = vertexColorBuffer->GetGPUVirtualAddress();
            vbvs[1].SizeInBytes              = static_cast<UINT>(vertexColorBuffer->GetDesc().Width);
            vbvs[1].StrideInBytes            = 12;

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
    D3D12_ROOT_PARAMETER rootParameter     = {};
    rootParameter.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.Num32BitValues = 16;
    rootParameter.Constants.ShaderRegister = 0;
    rootParameter.Constants.RegisterSpace  = 0;
    rootParameter.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 1;
    rootSigDesc.pParameters               = &rootParameter;
    rootSigDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    CHECK_CALL(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    CHECK_CALL(pRenderer->Device->CreateRootSignature(
        0,                         // nodeMask
        blob->GetBufferPointer(),  // pBloblWithRootSignature
        blob->GetBufferSize(),     // blobLengthInBytes
        IID_PPV_ARGS(ppRootSig))); // riid, ppvRootSignature
}

void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppVertexColorBuffer)
{
    TriMesh mesh = TriMesh::Cube(vec3(1), false, {.enableVertexColors = true});

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
        SizeInBytes(mesh.GetVertexColors()),
        DataPtr(mesh.GetVertexColors()),
        ppVertexColorBuffer));
}