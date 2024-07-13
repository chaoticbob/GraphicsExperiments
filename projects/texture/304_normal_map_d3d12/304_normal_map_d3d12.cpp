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
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static LPCWSTR gVSShaderName = L"vsmain";
static LPCWSTR gPSShaderName = L"psmain";

static float gTargetAngleX = 0.0f;
static float gAngleX       = 0.0f;
static float gTargetAngleY = 0.0f;
static float gAngleY       = 0.0f;

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateTextures(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppDiffuse,
    ID3D12Resource** ppNormal);
void CreateDescriptorHeaps(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppCBVSRVUAVHeap,
    ID3D12DescriptorHeap** ppSamplerHeap);
void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppVertexBuffer,
    ID3D12Resource** ppTexCoordBuffer,
    ID3D12Resource** ppNormalBuffer,
    ID3D12Resource** ppTangentBuffer,
    ID3D12Resource** ppBitangentBuffer);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    int dx = x - prevX;
    int dy = y - prevY;

    if (buttons & MOUSE_BUTTON_RIGHT) {
        gTargetAngleX += 0.25f * dy;
    }
    if (buttons & MOUSE_BUTTON_LEFT) {
        gTargetAngleY += 0.25f * dx;
    }

    prevX = x;
    prevY = y;
}

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
        std::string shaderSource = LoadString("projects/304_normal_map/shaders.hlsl");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &dxilVS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &dxilPS, &errorMsg);
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
    CHECK_CALL(CreateGraphicsPipeline1(
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
    ComPtr<ID3D12Resource> diffuseTexture;
    ComPtr<ID3D12Resource> normalTexture;
    CreateTextures(renderer.get(), &diffuseTexture, &normalTexture);

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> cbvsrvuavHeap;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    CreateDescriptorHeaps(renderer.get(), &cbvsrvuavHeap, &samplerHeap);
    {
        // Write texture descriptors
        {
            const UINT                  inc        = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart();

            // Diffuse
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                          = diffuseTexture->GetDesc().Format;
            srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip       = 0;
            srvDesc.Texture2D.MipLevels             = diffuseTexture->GetDesc().MipLevels;
            srvDesc.Texture2D.PlaneSlice            = 0;
            srvDesc.Texture2D.ResourceMinLODClamp   = 0;
            renderer->Device->CreateShaderResourceView(diffuseTexture.Get(), &srvDesc, descriptor);
            descriptor.ptr += inc;

            // Normal
            srvDesc.Format              = normalTexture->GetDesc().Format;
            srvDesc.Texture2D.MipLevels = normalTexture->GetDesc().MipLevels;
            renderer->Device->CreateShaderResourceView(normalTexture.Get(), &srvDesc, descriptor);
            descriptor.ptr += inc;
        }

        // Write sampler descriptor
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter             = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU           = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV           = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW           = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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
    uint32_t               numIndices;
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> texCoordBuffer;
    ComPtr<ID3D12Resource> normalBuffer;
    ComPtr<ID3D12Resource> tangentBuffer;
    ComPtr<ID3D12Resource> bitangentBuffer;
    CreateGeometryBuffers(
        renderer.get(),
        &indexBuffer,
        &numIndices,
        &positionBuffer,
        &texCoordBuffer,
        &normalBuffer,
        &tangentBuffer,
        &bitangentBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "304_normal_map_d3d12");
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

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

            // Smooth out the rotation
            gAngleX += (gTargetAngleX - gAngleX) * 0.1f;
            gAngleY += (gTargetAngleY - gAngleY) * 0.1f;

            mat4 modelMat = glm::rotate(glm::radians(gAngleY), vec3(0, 1, 0)) *
                            glm::rotate(glm::radians(gAngleX), vec3(1, 0, 0));

            vec3 eyePos      = vec3(0, 1.0f, 1.25f);
            mat4 viewMat     = glm::lookAt(eyePos, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
            mat4 projViewMat = projMat * viewMat;

            commandList->SetGraphicsRootSignature(rootSig.Get());

            // Camera (b0)
            commandList->SetGraphicsRoot32BitConstants(0, 16, &modelMat, 0);
            commandList->SetGraphicsRoot32BitConstants(0, 16, &projViewMat, 16);
            commandList->SetGraphicsRoot32BitConstants(0, 3, &eyePos, 32);
            // Texture0 (t1)
            commandList->SetGraphicsRootDescriptorTable(1, cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart());
            // Sampler0 (s2)
            commandList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation          = indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes             = static_cast<UINT>(indexBuffer->GetDesc().Width);
            ibv.Format                  = DXGI_FORMAT_R32_UINT;
            commandList->IASetIndexBuffer(&ibv);

            D3D12_VERTEX_BUFFER_VIEW vbvs[5] = {};
            vbvs[0].BufferLocation           = positionBuffer->GetGPUVirtualAddress();
            vbvs[0].SizeInBytes              = static_cast<UINT>(positionBuffer->GetDesc().Width);
            vbvs[0].StrideInBytes            = 12;
            vbvs[1].BufferLocation           = texCoordBuffer->GetGPUVirtualAddress();
            vbvs[1].SizeInBytes              = static_cast<UINT>(texCoordBuffer->GetDesc().Width);
            vbvs[1].StrideInBytes            = 8;
            vbvs[2].BufferLocation           = normalBuffer->GetGPUVirtualAddress();
            vbvs[2].SizeInBytes              = static_cast<UINT>(normalBuffer->GetDesc().Width);
            vbvs[2].StrideInBytes            = 12;
            vbvs[3].BufferLocation           = tangentBuffer->GetGPUVirtualAddress();
            vbvs[3].SizeInBytes              = static_cast<UINT>(tangentBuffer->GetDesc().Width);
            vbvs[3].StrideInBytes            = 12;
            vbvs[4].BufferLocation           = bitangentBuffer->GetGPUVirtualAddress();
            vbvs[4].SizeInBytes              = static_cast<UINT>(bitangentBuffer->GetDesc().Width);
            vbvs[4].StrideInBytes            = 12;

            commandList->IASetVertexBuffers(0, 5, vbvs);

            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

            commandList->SetPipelineState(pipelineState.Get());

            commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
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
    ranges[0].NumDescriptors                    = 2;
    ranges[0].BaseShaderRegister                = 1;
    ranges[0].RegisterSpace                     = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors                    = 1;
    ranges[1].BaseShaderRegister                = 4;
    ranges[1].RegisterSpace                     = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3]                = {};
    rootParameters[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.Num32BitValues            = 35;
    rootParameters[0].Constants.ShaderRegister            = 0;
    rootParameters[0].Constants.RegisterSpace             = 0;
    rootParameters[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
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

void CreateTextures(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppDiffuse,
    ID3D12Resource** ppNormal)
{
    auto dir = GetAssetPath("textures/metal_grate_rusty");

    // Diffuse
    {
        auto mipmap = MipmapRGBA8u::MipmapT(
            LoadImage8u(dir / "diffuse.png"),
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_FILTER_MODE_LINEAR);
        assert((mipmap.GetSizeInBytes() > 0) && "diffuse image load failed");

        CHECK_CALL(CreateTexture(
            pRenderer,
            mipmap.GetWidth(0),
            mipmap.GetHeight(0),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            mipmap.GetMipOffsets(),
            mipmap.GetSizeInBytes(),
            mipmap.GetPixels(),
            ppDiffuse));
    }

    // Normal
    {
        auto mipmap = MipmapRGBA8u::MipmapT(
            LoadImage8u(dir / "normal_dx.png"),
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_FILTER_MODE_LINEAR);
        assert((mipmap.GetSizeInBytes() > 0) && "normal image load failed");

        CHECK_CALL(CreateTexture(
            pRenderer,
            mipmap.GetWidth(0),
            mipmap.GetHeight(0),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            mipmap.GetMipOffsets(),
            mipmap.GetSizeInBytes(),
            mipmap.GetPixels(),
            ppNormal));
    }
}

void CreateDescriptorHeaps(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppCBVSRVUAVHeap, ID3D12DescriptorHeap** ppSamplerHeap)
{
    //
    // CBVSRVUAV heap
    //
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors             = 2;
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
    uint32_t*        pNumIndices,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppTexCoordBuffer,
    ID3D12Resource** ppNormalBuffer,
    ID3D12Resource** ppTangentBuffer,
    ID3D12Resource** ppBitangentBuffer)
{
    TriMesh::Options options = {.enableTexCoords = true, .enableNormals = true, .enableTangents = true};
    TriMesh          mesh    = TriMesh::Cube(vec3(1), false, options);

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        ppIndexBuffer));

    *pNumIndices = mesh.GetNumIndices();

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

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        ppNormalBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTangents()),
        DataPtr(mesh.GetTangents()),
        ppTangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetBitangents()),
        DataPtr(mesh.GetBitangents()),
        ppBitangentBuffer));
}
