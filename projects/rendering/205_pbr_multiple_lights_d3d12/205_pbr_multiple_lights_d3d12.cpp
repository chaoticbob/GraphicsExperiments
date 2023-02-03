#include "window.h"

#include "dx_renderer.h"
#include "bitmap.h"
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

// This will be passed in via constant buffer
struct Light
{
    vec3     position;
    uint32_t __pad;
    vec3     color;
    float    intensity;
};

struct SceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t numLights;
    Light    lights[8];
};

struct DrawParameters
{
    // These will be passed in via root constants
    mat4     modelMatrix;
    uint32_t materialIndex = 0;

    // Set at in the command list
    uint32_t               numIndices  = 0;
    ComPtr<ID3D12Resource> indexBuffer = nullptr;
};

struct MaterialParameters
{
    uint32_t UseGeometricNormal;
};

struct MaterialTextures
{
    ComPtr<ID3D12Resource> albedoTexture;
    ComPtr<ID3D12Resource> normalTexture;
    ComPtr<ID3D12Resource> roughnessTexture;
    ComPtr<ID3D12Resource> metalnessTexture;
};

struct VertexBuffers
{
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> texCoordBuffer;
    ComPtr<ID3D12Resource> normalBuffer;
    ComPtr<ID3D12Resource> tangentBuffer;
    ComPtr<ID3D12Resource> bitangentBuffer;
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

static LPCWSTR gVSShaderName = L"vsmain";
static LPCWSTR gPSShaderName = L"psmain";

float gTargetAngle = 0.0f;
float gAngle       = 0.0f;

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateMaterials(
    DxRenderer*                    pRenderer,
    const TriMesh*                 pMesh,
    ID3D12Resource**               ppMaterialParamsBuffer,
    MaterialTextures&              outDefaultMaterialTextures,
    std::vector<MaterialTextures>& outMatrialTextures);
void CreateDescriptorHeap(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppHeap);
void CreateVertexBuffers(
    DxRenderer*                  pRenderer,
    const TriMesh*               pMesh,
    std::vector<DrawParameters>& outDrawParams,
    VertexBuffers&               outVertexBuffers);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT) {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;
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
        std::string shaderSource = LoadString("projects/205_pbr_multiple_lights_d3d12/shaders.hlsl");

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
    // Constant buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> constantBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        Align<size_t>(sizeof(SceneParameters), 256),
        nullptr,
        &constantBuffer));

    // *************************************************************************
    // Load mesh
    // *************************************************************************
    std::unique_ptr<TriMesh> mesh;
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.invertTexCoordsV = true;

        mesh = std::make_unique<TriMesh>(options);
        if (!mesh) {
            assert(false && "allocated mesh failed");
            return EXIT_FAILURE;
        }

        if (!TriMesh::LoadOBJ(GetAssetPath("models/camera/Camera.obj").string(), GetAssetPath("models/camera").string(), options, mesh.get())) {
            assert(false && "OBJ load failed");
            return EXIT_FAILURE;
        }

        mesh->Recenter();

        auto bounds = mesh->GetBounds();

        std::stringstream ss;
        ss << "mesh bounding box: "
           << "min = (" << bounds.min.x << ", " << bounds.min.y << ", " << bounds.min.z << ")"
           << " "
           << "max = (" << bounds.max.x << ", " << bounds.max.y << ", " << bounds.max.z << ")";
        GREX_LOG_INFO(ss.str());
    }

    // *************************************************************************
    // Materials
    // *************************************************************************
    ComPtr<ID3D12Resource>        materialParamsBuffer;
    MaterialTextures              defaultMaterialTextures = {};
    std::vector<MaterialTextures> materialTexturesSets    = {};
    CreateMaterials(
        renderer.get(),
        mesh.get(),
        &materialParamsBuffer,
        defaultMaterialTextures,
        materialTexturesSets);

    // *************************************************************************
    // Descriptor heap
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> cbvsrvuavHeap;
    CreateDescriptorHeap(renderer.get(), &cbvsrvuavHeap);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart();
        for (auto& materialTextures : materialTexturesSets) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip       = 0;
            srvDesc.Texture2D.MipLevels             = 1;
            srvDesc.Texture2D.PlaneSlice            = 0;
            srvDesc.Texture2D.ResourceMinLODClamp   = 0;

            srvDesc.Format = materialTextures.albedoTexture->GetDesc().Format;
            renderer->Device->CreateShaderResourceView(materialTextures.albedoTexture.Get(), &srvDesc, descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            srvDesc.Format = materialTextures.normalTexture->GetDesc().Format;
            renderer->Device->CreateShaderResourceView(materialTextures.normalTexture.Get(), &srvDesc, descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            srvDesc.Format = materialTextures.roughnessTexture->GetDesc().Format;
            renderer->Device->CreateShaderResourceView(materialTextures.roughnessTexture.Get(), &srvDesc, descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            srvDesc.Format = materialTextures.metalnessTexture->GetDesc().Format;
            renderer->Device->CreateShaderResourceView(materialTextures.metalnessTexture.Get(), &srvDesc, descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    // *************************************************************************
    // Vertex buffers
    // *************************************************************************
    std::vector<DrawParameters> drawParams    = {};
    VertexBuffers               vertexBuffers = {};
    CreateVertexBuffers(
        renderer.get(),
        mesh.get(),
        drawParams,
        vertexBuffers);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "205_pbr_multiple_lights_d3d12");
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
    // Persistent map scene parameters
    // *************************************************************************
    SceneParameters* pSceneParams = nullptr;
    CHECK_CALL(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pSceneParams)));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

        ComPtr<ID3D12Resource> swapchainBuffer;
        CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        // Set descriptor heaps
        ID3D12DescriptorHeap* pDescriptorHeaps[12] = {cbvsrvuavHeap.Get()};
        commandList->SetDescriptorHeaps(1, pDescriptorHeaps);

        D3D12_RESOURCE_BARRIER preRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &preRenderBarrier);
        {
            commandList->OMSetRenderTargets(
                1,
                &renderer->SwapchainRTVDescriptorHandles[bufferIndex],
                false,
                &renderer->SwapchainDSVDescriptorHandles[bufferIndex]);

            // Clear RTV and DSV
            float clearColor[4] = {0.23f, 0.23f, 0.31f, 0};
            commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 0, nullptr);
            commandList->ClearDepthStencilView(renderer->SwapchainDSVDescriptorHandles[bufferIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 0, nullptr);

            // Smooth out the rotation on Y
            gAngle += (gTargetAngle - gAngle) * 0.1f;

            // Camera matrices
            vec3 eyePosition = vec3(0, 4.5f, 8);
            mat4 modelMat    = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));
            mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, -0.25f, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

            // Set constant buffer values
            pSceneParams->viewProjectionMatrix = projMat * viewMat;
            pSceneParams->eyePosition          = eyePosition;
            pSceneParams->numLights            = 4;
            pSceneParams->lights[0].position   = vec3(5, 7, 32);
            pSceneParams->lights[0].color      = vec3(0.98f, 0.85f, 0.71f);
            pSceneParams->lights[0].intensity  = 0.8f;
            pSceneParams->lights[1].position   = vec3(-8, 1, 4);
            pSceneParams->lights[1].color      = vec3(0.85f, 0.95f, 0.81f);
            pSceneParams->lights[1].intensity  = 0.4f;
            pSceneParams->lights[2].position   = vec3(0, 8, -8);
            pSceneParams->lights[2].color      = vec3(0.89f, 0.89f, 0.97f);
            pSceneParams->lights[2].intensity  = 0.95f;
            pSceneParams->lights[3].position   = vec3(15, 0, 0);
            pSceneParams->lights[3].color      = vec3(0.92f, 0.5f, 0.7f);
            pSceneParams->lights[3].intensity  = 0.5f;

            commandList->SetGraphicsRootSignature(rootSig.Get());
            // Camera (b0)
            commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
            // MaterialParams (t2)
            commandList->SetGraphicsRootShaderResourceView(2, materialParamsBuffer->GetGPUVirtualAddress());
            // MaterialTextures (t10)
            commandList->SetGraphicsRootDescriptorTable(3, cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart());

            // Vertex buffers
            D3D12_VERTEX_BUFFER_VIEW vbvs[5] = {};
            // Position
            vbvs[0].BufferLocation = vertexBuffers.positionBuffer->GetGPUVirtualAddress();
            vbvs[0].SizeInBytes    = static_cast<UINT>(vertexBuffers.positionBuffer->GetDesc().Width);
            vbvs[0].StrideInBytes  = 12;
            // TexCoord
            vbvs[1].BufferLocation = vertexBuffers.texCoordBuffer->GetGPUVirtualAddress();
            vbvs[1].SizeInBytes    = static_cast<UINT>(vertexBuffers.texCoordBuffer->GetDesc().Width);
            vbvs[1].StrideInBytes  = 8;
            // Normal
            vbvs[2].BufferLocation = vertexBuffers.normalBuffer->GetGPUVirtualAddress();
            vbvs[2].SizeInBytes    = static_cast<UINT>(vertexBuffers.normalBuffer->GetDesc().Width);
            vbvs[2].StrideInBytes  = 12;
            // Tangent
            vbvs[3].BufferLocation = vertexBuffers.tangentBuffer->GetGPUVirtualAddress();
            vbvs[3].SizeInBytes    = static_cast<UINT>(vertexBuffers.tangentBuffer->GetDesc().Width);
            vbvs[3].StrideInBytes  = 12;
            // Bitangent
            vbvs[4].BufferLocation = vertexBuffers.bitangentBuffer->GetGPUVirtualAddress();
            vbvs[4].SizeInBytes    = static_cast<UINT>(vertexBuffers.bitangentBuffer->GetDesc().Width);
            vbvs[4].StrideInBytes  = 12;

            commandList->IASetVertexBuffers(0, 5, vbvs);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Viewport and scissor
            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);
            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

            // Pipeline state
            commandList->SetPipelineState(pipelineState.Get());

            for (auto& draw : drawParams) {
                // Index buffer
                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation          = draw.indexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes             = static_cast<UINT>(draw.indexBuffer->GetDesc().Width);
                ibv.Format                  = DXGI_FORMAT_R32_UINT;
                commandList->IASetIndexBuffer(&ibv);

                // DrawParams (b1)
                commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                commandList->SetGraphicsRoot32BitConstants(1, 1, &draw.materialIndex, 16);

                commandList->DrawIndexedInstanced(draw.numIndices, 1, 0, 0, 0);
            }
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
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 8;
    range.BaseShaderRegister                = 10;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    // SceneParams (b0)
    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // DrawParams (b1)
    rootParameters[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].Constants.Num32BitValues = 17;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.RegisterSpace  = 0;
    rootParameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // MaterialParams (t2)
    rootParameters[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // MaterialTextures (t10)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &range;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter                    = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSampler.AddressU                  = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV                  = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW                  = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MipLODBias                = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSampler.MaxAnisotropy             = 0;
    staticSampler.ComparisonFunc            = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSampler.MinLOD                    = 0;
    staticSampler.MaxLOD                    = 1;
    staticSampler.ShaderRegister            = 9;
    staticSampler.RegisterSpace             = 0;
    staticSampler.ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 4;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.NumStaticSamplers         = 1;
    rootSigDesc.pStaticSamplers           = &staticSampler;
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

void CreateMaterials(
    DxRenderer*                    pRenderer,
    const TriMesh*                 pMesh,
    ID3D12Resource**               ppMaterialParamsBuffer,
    MaterialTextures&              outDefaultMaterialTextures,
    std::vector<MaterialTextures>& outMatrialTexturesSets)
{
    // Default material textures
    {
        PixelRGBA8u purplePixel = {1, 0, 1, 1};
        PixelRGBA8u blackPixel  = {0, 0, 0, 1};

        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &purplePixel, &outDefaultMaterialTextures.albedoTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blackPixel, &outDefaultMaterialTextures.normalTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blackPixel, &outDefaultMaterialTextures.roughnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blackPixel, &outDefaultMaterialTextures.metalnessTexture));
    }

    // Materials
    std::vector<MaterialParameters> materialParamsList;
    for (uint32_t materialIndex = 0; materialIndex < pMesh->GetNumMaterials(); ++materialIndex) {
        auto& material = pMesh->GetMaterial(materialIndex);

        // Material params
        MaterialParameters materialParams = {};
        if (material.name == "LensMaterial") {
            materialParams.UseGeometricNormal = 1;
        }
        materialParamsList.push_back(materialParams);

        // Material textures
        MaterialTextures materialTextures = outDefaultMaterialTextures;
        if (!material.albedoTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(GetAssetPath(fs::path("models/camera/") / material.albedoTexture));
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (albedo) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetPixels(),
                &materialTextures.albedoTexture));
        }
        if (!material.normalTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(GetAssetPath(fs::path("models/camera/") / material.normalTexture));
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (normal) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetPixels(),
                &materialTextures.normalTexture));
        }
        if (!material.roughnessTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(GetAssetPath(fs::path("models/camera/") / material.roughnessTexture));
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (roughness) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetPixels(),
                &materialTextures.roughnessTexture));
        }
        if (!material.metalnessTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(GetAssetPath(fs::path("models/camera/") / material.metalnessTexture));
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (metalness) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetPixels(),
                &materialTextures.metalnessTexture));
        }

        outMatrialTexturesSets.push_back(materialTextures);
    }

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(materialParamsList),
        DataPtr(materialParamsList),
        ppMaterialParamsBuffer));
}

void CreateDescriptorHeap(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors             = 8;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ppHeap)));
}

void CreateVertexBuffers(
    DxRenderer*                  pRenderer,
    const TriMesh*               pMesh,
    std::vector<DrawParameters>& outDrawParams,
    VertexBuffers&               outVertexBuffers)
{
    // Group draws based on material indices
    for (uint32_t materialIndex = 0; materialIndex < pMesh->GetNumMaterials(); ++materialIndex) {
        auto triangles = pMesh->GetTrianglesForMaterial(materialIndex);

        DrawParameters params = {};
        params.numIndices     = static_cast<uint32_t>(3 * triangles.size());
        params.materialIndex  = materialIndex;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(triangles),
            DataPtr(triangles),
            &params.indexBuffer));

        outDrawParams.push_back(params);
    }

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetPositions()),
        DataPtr(pMesh->GetPositions()),
        &outVertexBuffers.positionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetTexCoords()),
        DataPtr(pMesh->GetTexCoords()),
        &outVertexBuffers.texCoordBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetNormals()),
        DataPtr(pMesh->GetNormals()),
        &outVertexBuffers.normalBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetTangents()),
        DataPtr(pMesh->GetTangents()),
        &outVertexBuffers.tangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(pMesh->GetBitangents()),
        DataPtr(pMesh->GetBitangents()),
        &outVertexBuffers.bitangentBuffer));
}