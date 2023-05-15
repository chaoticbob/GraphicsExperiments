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
    uint     iblEnvNumLevels;
};

struct DrawParameters
{
    mat4     modelMatrix;
    uint32_t materialIndex = 0;

    uint32_t               numIndices  = 0;
    ComPtr<ID3D12Resource> indexBuffer = nullptr;
};

struct MaterialParameters
{
    uint32_t UseGeometricNormal;
};

struct MaterialTextures
{
    ComPtr<ID3D12Resource> baseColorTexture;
    ComPtr<ID3D12Resource> normalTexture;
    ComPtr<ID3D12Resource> roughnessTexture;
    ComPtr<ID3D12Resource> metalnessTexture;
    ComPtr<ID3D12Resource> aoTexture;
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
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static LPCWSTR gVSShaderName = L"vsmain";
static LPCWSTR gPSShaderName = L"psmain";

float gTargetAngle = 0.0f;
float gAngle       = 0.0f;

static uint32_t gNumLights = 0;

void CreateRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateEnvironmentRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateIBLTextures(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppBRDFLUT,
    ID3D12Resource** ppIrradianceTexture,
    ID3D12Resource** ppEnvironmentTexture,
    uint32_t*        pEnvNumLevels);
void CreateCameraMaterials(
    DxRenderer*                    pRenderer,
    const TriMesh*                 pMesh,
    const fs::path&                textureDir,
    ID3D12Resource**               ppMaterialParamsBuffer,
    MaterialTextures&              outDefaultMaterialTextures,
    std::vector<MaterialTextures>& outMatrialTextures);
void CreateDescriptorHeap(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppHeap);
void CreateCameraVertexBuffers(
    DxRenderer*                  pRenderer,
    const TriMesh*               pMesh,
    std::vector<DrawParameters>& outDrawParams,
    VertexBuffers&               outVertexBuffers);
void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppTexCoordBuffer);

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
    // PBR shaders
    std::vector<char> dxilVS;
    std::vector<char> dxilPS;
    {
        std::string shaderSource = LoadString("projects/203_pbr_camera_d3d12/shaders.hlsl");

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
    // Draw texture shaders
    std::vector<char> drawTextureDxilVS;
    std::vector<char> drawTextureDxilPS;
    {
        std::string shaderSource = LoadString("projects/203_pbr_camera_d3d12/drawtexture.hlsl");
        if (shaderSource.empty()) {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &drawTextureDxilVS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &drawTextureDxilPS, &errorMsg);
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
    // PBR root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature> pbrRootSig;
    CreateRootSig(renderer.get(), &pbrRootSig);

    // *************************************************************************
    // Environment root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature> envRootSig;
    CreateEnvironmentRootSig(renderer.get(), &envRootSig);

    // *************************************************************************
    // PBR pipeline state object
    // *************************************************************************
    ComPtr<ID3D12PipelineState> pbrPipelineState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        pbrRootSig.Get(),
        dxilVS,
        dxilPS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pbrPipelineState));

    // *************************************************************************
    // Environment pipeline state object
    // *************************************************************************
    ComPtr<ID3D12PipelineState> envPipelineState;
    CHECK_CALL(CreateDrawTexturePipeline(
        renderer.get(),
        envRootSig.Get(),
        drawTextureDxilVS,
        drawTextureDxilPS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &envPipelineState,
        D3D12_CULL_MODE_FRONT));

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
    const fs::path           modelDir  = "models/camera";
    const fs::path           modelFile = modelDir / "camera.obj";
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

        if (!TriMesh::LoadOBJ(GetAssetPath(modelFile).string(), GetAssetPath(modelDir).string(), options, mesh.get())) {
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
    CreateCameraMaterials(
        renderer.get(),
        mesh.get(),
        GetAssetPath(fs::path(modelDir)),
        &materialParamsBuffer,
        defaultMaterialTextures,
        materialTexturesSets);

    // *************************************************************************
    // Environment texture
    // *************************************************************************
    ComPtr<ID3D12Resource> brdfLUT;
    ComPtr<ID3D12Resource> irrTexture;
    ComPtr<ID3D12Resource> envTexture;
    uint32_t               envNumLevels = 0;
    CreateIBLTextures(renderer.get(), &brdfLUT, &irrTexture, &envTexture, &envNumLevels);

    // *************************************************************************
    // Descriptor heap
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    CreateDescriptorHeap(renderer.get(), &descriptorHeap);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        // IBL integration LUT
        CreateDescriptorTexture2D(renderer.get(), brdfLUT.Get(), descriptor);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // Irradiance map
        CreateDescriptorTexture2D(renderer.get(), irrTexture.Get(), descriptor);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // Environment map
        CreateDescriptorTexture2D(renderer.get(), envTexture.Get(), descriptor, 0, envNumLevels);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Material textures
        for (auto& materialTextures : materialTexturesSets) {
            // Albedo
            CreateDescriptorTexture2D(renderer.get(), materialTextures.baseColorTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Normal
            CreateDescriptorTexture2D(renderer.get(), materialTextures.normalTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Roughness
            CreateDescriptorTexture2D(renderer.get(), materialTextures.roughnessTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Metalness
            CreateDescriptorTexture2D(renderer.get(), materialTextures.metalnessTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Ambient Occlusion
            CreateDescriptorTexture2D(renderer.get(), materialTextures.aoTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    // *************************************************************************
    // Camera Vertex buffers
    // *************************************************************************
    std::vector<DrawParameters> cameraDrawParams    = {};
    VertexBuffers               cameraVertexBuffers = {};
    CreateCameraVertexBuffers(
        renderer.get(),
        mesh.get(),
        cameraDrawParams,
        cameraVertexBuffers);

    // *************************************************************************
    // Environment vertex buffers
    // *************************************************************************
    uint32_t               envNumIndices = 0;
    ComPtr<ID3D12Resource> envIndexBuffer;
    ComPtr<ID3D12Resource> envPositionBuffer;
    ComPtr<ID3D12Resource> envTexCoordBuffer;
    CreateEnvironmentVertexBuffers(
        renderer.get(),
        &envNumIndices,
        &envIndexBuffer,
        &envPositionBuffer,
        &envTexCoordBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "203_pbr_camera_d3d12");
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
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForD3D12(renderer.get())) {
        assert(false && "Window::InitImGuiForD3D12 failed");
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
        window->ImGuiNewFrameD3D12();

        if (ImGui::Begin("Scene")) {
            ImGui::SliderInt("Number of Lights", reinterpret_cast<int*>(&gNumLights), 0, 4);
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

        ComPtr<ID3D12Resource> swapchainBuffer;
        CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        // Set descriptor heaps
        ID3D12DescriptorHeap* pDescriptorHeaps[12] = {descriptorHeap.Get()};
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

            // Viewport and scissor
            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);
            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

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
            pSceneParams->numLights            = gNumLights;
            pSceneParams->lights[0].position   = vec3(5, 7, 32);
            pSceneParams->lights[0].color      = vec3(1.00f, 0.70f, 0.00f);
            pSceneParams->lights[0].intensity  = 0.2f;
            pSceneParams->lights[1].position   = vec3(-8, 1, 4);
            pSceneParams->lights[1].color      = vec3(1.00f, 0.00f, 0.00f);
            pSceneParams->lights[1].intensity  = 0.4f;
            pSceneParams->lights[2].position   = vec3(0, 8, -8);
            pSceneParams->lights[2].color      = vec3(0.00f, 1.00f, 0.00f);
            pSceneParams->lights[2].intensity  = 0.4f;
            pSceneParams->lights[3].position   = vec3(15, 8, 0);
            pSceneParams->lights[3].color      = vec3(0.00f, 0.00f, 1.00f);
            pSceneParams->lights[3].intensity  = 0.4f;
            pSceneParams->iblEnvNumLevels      = envNumLevels;

            // Draw environment
            {
                commandList->SetGraphicsRootSignature(envRootSig.Get());
                commandList->SetPipelineState(envPipelineState.Get());

                glm::mat4 moveUp = glm::translate(vec3(0, 0, 0));

                // SceneParmas (b0)
                mat4 mvp = projMat * viewMat * moveUp;
                commandList->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);
                // Textures (32)
                D3D12_GPU_DESCRIPTOR_HANDLE tableStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += 2 * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                commandList->SetGraphicsRootDescriptorTable(1, tableStart);

                // Index buffer
                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation          = envIndexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes             = static_cast<UINT>(envIndexBuffer->GetDesc().Width);
                ibv.Format                  = DXGI_FORMAT_R32_UINT;
                commandList->IASetIndexBuffer(&ibv);

                // Vertex buffers
                D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
                // Position
                vbvs[0].BufferLocation = envPositionBuffer->GetGPUVirtualAddress();
                vbvs[0].SizeInBytes    = static_cast<UINT>(envPositionBuffer->GetDesc().Width);
                vbvs[0].StrideInBytes  = 12;
                // Tex coord
                vbvs[1].BufferLocation = envTexCoordBuffer->GetGPUVirtualAddress();
                vbvs[1].SizeInBytes    = static_cast<UINT>(envTexCoordBuffer->GetDesc().Width);
                vbvs[1].StrideInBytes  = 8;

                commandList->IASetVertexBuffers(0, 2, vbvs);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                commandList->DrawIndexedInstanced(envNumIndices, 1, 0, 0, 0);
            }

            // Draw camera
            {
                commandList->SetGraphicsRootSignature(pbrRootSig.Get());
                // SceneParams (b0)
                commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
                // MaterialParams (t2)
                commandList->SetGraphicsRootShaderResourceView(2, materialParamsBuffer->GetGPUVirtualAddress());
                // IBL textures (t3, t4, t5)
                D3D12_GPU_DESCRIPTOR_HANDLE tableStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
                commandList->SetGraphicsRootDescriptorTable(3, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
                // MaterialTextures (t10)
                tableStart.ptr += 3 * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                commandList->SetGraphicsRootDescriptorTable(4, tableStart);

                // Vertex buffers
                D3D12_VERTEX_BUFFER_VIEW vbvs[5] = {};
                // Position
                vbvs[0].BufferLocation = cameraVertexBuffers.positionBuffer->GetGPUVirtualAddress();
                vbvs[0].SizeInBytes    = static_cast<UINT>(cameraVertexBuffers.positionBuffer->GetDesc().Width);
                vbvs[0].StrideInBytes  = 12;
                // TexCoord
                vbvs[1].BufferLocation = cameraVertexBuffers.texCoordBuffer->GetGPUVirtualAddress();
                vbvs[1].SizeInBytes    = static_cast<UINT>(cameraVertexBuffers.texCoordBuffer->GetDesc().Width);
                vbvs[1].StrideInBytes  = 8;
                // Normal
                vbvs[2].BufferLocation = cameraVertexBuffers.normalBuffer->GetGPUVirtualAddress();
                vbvs[2].SizeInBytes    = static_cast<UINT>(cameraVertexBuffers.normalBuffer->GetDesc().Width);
                vbvs[2].StrideInBytes  = 12;
                // Tangent
                vbvs[3].BufferLocation = cameraVertexBuffers.tangentBuffer->GetGPUVirtualAddress();
                vbvs[3].SizeInBytes    = static_cast<UINT>(cameraVertexBuffers.tangentBuffer->GetDesc().Width);
                vbvs[3].StrideInBytes  = 12;
                // Bitangent
                vbvs[4].BufferLocation = cameraVertexBuffers.bitangentBuffer->GetGPUVirtualAddress();
                vbvs[4].SizeInBytes    = static_cast<UINT>(cameraVertexBuffers.bitangentBuffer->GetDesc().Width);
                vbvs[4].StrideInBytes  = 12;

                commandList->IASetVertexBuffers(0, 5, vbvs);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // Pipeline state
                commandList->SetPipelineState(pbrPipelineState.Get());

                for (auto& draw : cameraDrawParams) {
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

            // Draw ImGui
            window->ImGuiRenderDrawData(renderer.get(), commandList.Get());
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

void CreateRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    // IBL textures (t3, t4, t5)
    D3D12_DESCRIPTOR_RANGE iblRange            = {};
    iblRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblRange.NumDescriptors                    = 3;
    iblRange.BaseShaderRegister                = 3;
    iblRange.RegisterSpace                     = 0;
    iblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // MaterialTextures (t10)
    D3D12_DESCRIPTOR_RANGE materialRange            = {};
    materialRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialRange.NumDescriptors                    = 10;
    materialRange.BaseShaderRegister                = 10;
    materialRange.RegisterSpace                     = 0;
    materialRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};
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
    // IBL textures (t3, t4, t5)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &iblRange;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    // MaterialTextures (t10)
    rootParameters[4].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges   = &materialRange;
    rootParameters[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
    // IBLIntegrationSampler (s6)
    staticSamplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[0].MaxAnisotropy    = 0;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].MinLOD           = 0;
    staticSamplers[0].MaxLOD           = 1;
    staticSamplers[0].ShaderRegister   = 6;
    staticSamplers[0].RegisterSpace    = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // IBLMapSampler (s7)
    staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MinLOD           = 0;
    staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister   = 7;
    staticSamplers[1].RegisterSpace    = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // MaterialSampler (s9)
    staticSamplers[2].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[2].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[2].MaxAnisotropy    = 0;
    staticSamplers[2].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[2].MinLOD           = 0;
    staticSamplers[2].MaxLOD           = 1;
    staticSamplers[2].ShaderRegister   = 9;
    staticSamplers[2].RegisterSpace    = 0;
    staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 5;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.NumStaticSamplers         = 3;
    rootSigDesc.pStaticSamplers           = staticSamplers;
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

void CreateEnvironmentRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    // Textures (t2)
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = 2;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    // SceneParams (b0)
    rootParameters[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.Num32BitValues = 17;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // Textures (t2)
    rootParameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges   = &range;
    rootParameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    // Sampler0 (s1)
    staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSampler.MaxAnisotropy    = 0;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSampler.MinLOD           = 0;
    staticSampler.MaxLOD           = 1;
    staticSampler.ShaderRegister   = 1;
    staticSampler.RegisterSpace    = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 2;
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

void CreateCameraMaterials(
    DxRenderer*                    pRenderer,
    const TriMesh*                 pMesh,
    const fs::path&                textureDir,
    ID3D12Resource**               ppMaterialParamsBuffer,
    MaterialTextures&              outDefaultMaterialTextures,
    std::vector<MaterialTextures>& outMatrialTexturesSets)
{
    // Default material textures
    {
        PixelRGBA8u purplePixel = {0, 0, 0, 255};
        PixelRGBA8u blackPixel  = {0, 0, 0, 255};
        PixelRGBA8u whitePixel  = {255, 255, 255, 255};

        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &purplePixel, &outDefaultMaterialTextures.baseColorTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.normalTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.roughnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.metalnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &whitePixel, &outDefaultMaterialTextures.aoTexture));
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
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.albedoTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (albedo) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.baseColorTexture));
        }
        if (!material.normalTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.normalTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (normal) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.normalTexture));
        }
        if (!material.roughnessTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.roughnessTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (roughness) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.roughnessTexture));
        }
        if (!material.metalnessTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.metalnessTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (metalness) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.metalnessTexture));
        }
        if (!material.aoTexture.empty()) {
            BitmapRGBA8u bitmap = LoadImage8u(textureDir / material.aoTexture);
            if (bitmap.GetSizeInBytes() == 0) {
                assert(false && "texture load (ambient occlusion) false");
            }
            CHECK_CALL(CreateTexture(
                pRenderer,
                bitmap.GetWidth(),
                bitmap.GetHeight(),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                bitmap.GetSizeInBytes(),
                bitmap.GetPixels(),
                &materialTextures.aoTexture));
        }

        outMatrialTexturesSets.push_back(materialTextures);
    }

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(materialParamsList),
        DataPtr(materialParamsList),
        ppMaterialParamsBuffer));
}

void CreateIBLTextures(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppBRDFLUT,
    ID3D12Resource** ppIrradianceTexture,
    ID3D12Resource** ppEnvironmentTexture,
    uint32_t*        pEnvNumLevels)
{
    // BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
        if (bitmap.Empty()) {
            assert(false && "Load image failed");
            return;
        }

        ComPtr<ID3D12Resource> texture;
        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            ppBRDFLUT));
    }

    // IBL file
    auto iblFile = GetAssetPath("IBL/palermo_square_4k.ibl");

    IBLMaps ibl = {};
    if (!LoadIBLMaps32f(iblFile, &ibl)) {
        GREX_LOG_ERROR("failed to load: " << iblFile);
        return;
    }

    *pEnvNumLevels = ibl.numLevels;

    // Irradiance
    {
        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.irradianceMap.GetWidth(),
            ibl.irradianceMap.GetHeight(),
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            ibl.irradianceMap.GetSizeInBytes(),
            ibl.irradianceMap.GetPixels(),
            ppIrradianceTexture));
    }

    // Environment
    {
        const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
        const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

        std::vector<DxMipOffset> mipOffsets;
        uint32_t                 levelOffset = 0;
        uint32_t                 levelWidth  = ibl.baseWidth;
        uint32_t                 levelHeight = ibl.baseHeight;
        for (uint32_t i = 0; i < ibl.numLevels; ++i) {
            DxMipOffset mipOffset = {};
            mipOffset.offset      = levelOffset;
            mipOffset.rowStride   = rowStride;

            mipOffsets.push_back(mipOffset);

            levelOffset += (rowStride * levelHeight);
            levelWidth >>= 1;
            levelHeight >>= 1;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.baseWidth,
            ibl.baseHeight,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            mipOffsets,
            ibl.environmentMap.GetSizeInBytes(),
            ibl.environmentMap.GetPixels(),
            ppEnvironmentTexture));
    }

    GREX_LOG_INFO("Loaded " << iblFile);
}

void CreateDescriptorHeap(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors             = 256;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ppHeap)));
}

void CreateCameraVertexBuffers(
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

void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppTexCoordBuffer)
{
    TriMesh mesh = TriMesh::Sphere(100, 64, 64, {.enableTexCoords = true, .faceInside = true});

    *pNumIndices = 3 * mesh.GetNumTriangles();

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