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
    uint     iblEnvironmentNumLevels;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    float metallic;
};

struct PBRImplementationInfo
{
    std::string description;
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 1024;
static bool     gEnableDebug  = true;

static LPCWSTR gVSShaderName = L"vsmain";
static LPCWSTR gPSShaderName = L"psmain";

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static uint32_t gNumLights = 0;

void CreatePBRRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateEnvironmentRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateMaterialSphereVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppNormalBuffer);
void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppTexCoordBuffer);
void CreateIBLTextures(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppBRDFLUT,
    ID3D12Resource** ppIrradianceTexture,
    ID3D12Resource** ppEnvironmentTexture,
    uint32_t*        pEnvNumLevels);
void CreateDescriptorHeap(
    DxRenderer*            pRenderer,
    ID3D12DescriptorHeap** ppHeap);

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
        std::string shaderSource = LoadString("projects/201_202_pbr_spheres/shaders.hlsl");

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
        std::string shaderSource = LoadString("projects/201_202_pbr_spheres/drawtexture.hlsl");
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
    CreatePBRRootSig(renderer.get(), &pbrRootSig);

    // *************************************************************************
    // Environment root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature> envRootSig;
    CreateEnvironmentRootSig(renderer.get(), &envRootSig);

    // *************************************************************************
    // PBR pipeline state object
    // *************************************************************************
    ComPtr<ID3D12PipelineState> pbrPipelineState;
    CHECK_CALL(CreateDrawNormalPipeline(
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
    // Material sphere vertex buffers
    // *************************************************************************
    uint32_t               materialSphereNumIndices = 0;
    ComPtr<ID3D12Resource> materialSphereIndexBuffer;
    ComPtr<ID3D12Resource> materialSpherePositionBuffer;
    ComPtr<ID3D12Resource> materialSphereNormalBuffer;
    CreateMaterialSphereVertexBuffers(
        renderer.get(),
        &materialSphereNumIndices,
        &materialSphereIndexBuffer,
        &materialSpherePositionBuffer,
        &materialSphereNormalBuffer);

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
    // IBL texture
    // *************************************************************************
    ComPtr<ID3D12Resource> brdfLUT;
    ComPtr<ID3D12Resource> irrTexture;
    ComPtr<ID3D12Resource> envTexture;
    uint32_t               envNumLevels = 0;
    CreateIBLTextures(renderer.get(), &brdfLUT, &irrTexture, &envTexture, &envNumLevels);

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    CreateDescriptorHeap(renderer.get(), &descriptorHeap);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

        // LUT
        CreateDescriptorTexture2D(renderer.get(), brdfLUT.Get(), descriptor);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Irradiance
        CreateDescriptorTexture2D(renderer.get(), irrTexture.Get(), descriptor);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Environment
        CreateDescriptorTexture2D(renderer.get(), envTexture.Get(), descriptor, 0, envNumLevels);
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "201_pbr_spheres_d3d12");
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

        // Descriptor heap
        ID3D12DescriptorHeap* heaps[1] = {descriptorHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);

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
            vec3 eyePosition = vec3(0, 0, 9);
            mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
            mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));

            // Set constant buffer values
            pSceneParams->viewProjectionMatrix    = projMat * viewMat;
            pSceneParams->eyePosition             = eyePosition;
            pSceneParams->numLights               = gNumLights;
            pSceneParams->lights[0].position      = vec3(5, 7, 32);
            pSceneParams->lights[0].color         = vec3(0.98f, 0.85f, 0.71f);
            pSceneParams->lights[0].intensity     = 0.5f;
            pSceneParams->lights[1].position      = vec3(-8, 1, 4);
            pSceneParams->lights[1].color         = vec3(1.00f, 0.00f, 0.00f);
            pSceneParams->lights[1].intensity     = 0.5f;
            pSceneParams->lights[2].position      = vec3(0, 8, -8);
            pSceneParams->lights[2].color         = vec3(0.00f, 1.00f, 0.00f);
            pSceneParams->lights[2].intensity     = 0.5f;
            pSceneParams->lights[3].position      = vec3(15, 8, 0);
            pSceneParams->lights[3].color         = vec3(0.00f, 0.00f, 1.00f);
            pSceneParams->lights[3].intensity     = 0.5f;
            pSceneParams->iblEnvironmentNumLevels = envNumLevels;

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

            // Draw material sphere
            {
                commandList->SetGraphicsRootSignature(pbrRootSig.Get());
                // SceneParams (b0)
                commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
                // IBL textures (t3, t4, t5)
                commandList->SetGraphicsRootDescriptorTable(3, descriptorHeap->GetGPUDescriptorHandleForHeapStart());

                // Index buffer
                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation          = materialSphereIndexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes             = static_cast<UINT>(materialSphereIndexBuffer->GetDesc().Width);
                ibv.Format                  = DXGI_FORMAT_R32_UINT;
                commandList->IASetIndexBuffer(&ibv);

                // Vertex buffers
                D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
                // Position
                vbvs[0].BufferLocation = materialSpherePositionBuffer->GetGPUVirtualAddress();
                vbvs[0].SizeInBytes    = static_cast<UINT>(materialSpherePositionBuffer->GetDesc().Width);
                vbvs[0].StrideInBytes  = 12;
                // Normal
                vbvs[1].BufferLocation = materialSphereNormalBuffer->GetGPUVirtualAddress();
                vbvs[1].SizeInBytes    = static_cast<UINT>(materialSphereNormalBuffer->GetDesc().Width);
                vbvs[1].StrideInBytes  = 12;

                commandList->IASetVertexBuffers(0, 2, vbvs);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // Pipeline state
                commandList->SetPipelineState(pbrPipelineState.Get());

                MaterialParameters materialParams = {};
                materialParams.baseColor          = vec3(0.8f, 0.8f, 0.9f);
                materialParams.roughness          = 0;
                materialParams.metallic           = 0;

                uint32_t numSlotsX     = 10;
                uint32_t numSlotsY     = 10;
                float    slotSize      = 0.9f;
                float    spanX         = numSlotsX * slotSize;
                float    spanY         = numSlotsY * slotSize;
                float    halfSpanX     = spanX / 2.0f;
                float    halfSpanY     = spanY / 2.0f;
                float    roughnessStep = 1.0f / (numSlotsX - 1);
                float    metalnessStep = 1.0f / (numSlotsY - 1);

                for (uint32_t i = 0; i < numSlotsY; ++i) {
                    materialParams.metallic = 0;

                    for (uint32_t j = 0; j < numSlotsX; ++j) {
                        float x = -halfSpanX + j * slotSize;
                        float y = -halfSpanY + i * slotSize;
                        float z = 0;
                        // Readjust center
                        x += slotSize / 2.0f;
                        y += slotSize / 2.0f;

                        glm::mat4 modelMat = rotMat * glm::translate(vec3(x, y, z));
                        // DrawParams (b1)
                        commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                        // MaterialParams (b2)
                        commandList->SetGraphicsRoot32BitConstants(2, 8, &materialParams, 0);

                        commandList->DrawIndexedInstanced(materialSphereNumIndices, 1, 0, 0, 0);

                        materialParams.metallic += metalnessStep;
                    }
                    materialParams.roughness += metalnessStep;
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

void CreatePBRRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    // IBL textures (t3, t4, t5)
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 3;
    range.BaseShaderRegister                = 3;
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
    rootParameters[1].Constants.Num32BitValues = 16;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.RegisterSpace  = 0;
    rootParameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // MaterialParams (t2)
    rootParameters[2].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[2].Constants.Num32BitValues = 8;
    rootParameters[2].Constants.ShaderRegister = 2;
    rootParameters[2].Constants.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // IBL textures (t3, t4, t5)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &range;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    // ClampedSampler (s6)
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
    // UWrapSampler (s7)
    staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].MipLODBias       = 0.5f; // D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MinLOD           = 0;
    staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister   = 7;
    staticSamplers[1].RegisterSpace    = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 4;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.NumStaticSamplers         = 2;
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
    // IBLEnvironmentMap (t2)
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = 2;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    // SceneParams (b0)
    rootParameters[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.Num32BitValues = 16;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // IBLEnvironmentMap (t2)
    rootParameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges   = &range;
    rootParameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    // IBLMapSampler (s1)
    staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSampler.MaxAnisotropy    = 0;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.MinLOD           = 0;
    staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
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

void CreateMaterialSphereVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppNormalBuffer)
{
    TriMesh mesh = TriMesh::Sphere(0.42f, 256, 256, {.enableNormals = true});

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
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        ppNormalBuffer));
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
    auto iblFile = GetAssetPath("IBL/old_depot_4k.ibl");

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

        std::vector<MipOffset> mipOffsets;
        uint32_t               levelOffset = 0;
        uint32_t               levelWidth  = ibl.baseWidth;
        uint32_t               levelHeight = ibl.baseHeight;
        for (uint32_t i = 0; i < ibl.numLevels; ++i) {
            MipOffset mipOffset = {};
            mipOffset.Offset    = levelOffset;
            mipOffset.RowStride = rowStride;

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

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(
        &desc,
        IID_PPV_ARGS(ppHeap)));
}