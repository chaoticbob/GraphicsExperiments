#include "window.h"

#include "dx_renderer.h"
#include "bitmap.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <fstream>

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

#define MATERIAL_TEXTURE_STRIDE 4
#define NUM_MATERIALS           9
#define TOTAL_MATERIAL_TEXTURES (NUM_MATERIALS * MATERIAL_TEXTURE_STRIDE)

#define IBL_INTEGRATION_LUT_DESCRIPTOR_OFFSET    3
#define IBL_INTEGRATION_MS_LUT_DESCRIPTOR_OFFSET 4
#define IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET    16
#define IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET   48
#define MATERIAL_TEXTURES_DESCRIPTOR_OFFSET      100

// This will be passed in via constant buffer
struct Light
{
    uint32_t active;
    vec3     position;
    vec3     color;
    float    intensity;
};

struct SceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t numLights;
    Light    lights[8];
    uint32_t iblNumEnvLevels;
    uint32_t iblIndex;
    uint     multiscatter;
    uint     colorCorrect;
};

struct MaterialParameters
{
    float specular;
};

struct MaterialTextures
{
    ComPtr<ID3D12Resource> baseColorTexture;
    ComPtr<ID3D12Resource> normalTexture;
    ComPtr<ID3D12Resource> roughnessTexture;
    ComPtr<ID3D12Resource> metallicTexture;
};

struct GeometryBuffers
{
    uint32_t               numIndices;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> texCoordBuffer;
    ComPtr<ID3D12Resource> normalBuffer;
    ComPtr<ID3D12Resource> tangentBuffer;
    ComPtr<ID3D12Resource> bitangentBuffer;
};

// =============================================================================
// Constants
// =============================================================================

const std::vector<std::string> gModelNames = {
    "Sphere",
    "Knob",
    "Monkey",
    "Cube",
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static LPCWSTR gVSShaderName = L"vsmain";
static LPCWSTR gPSShaderName = L"psmain";

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static std::vector<std::string> gMaterialNames = {};

static uint32_t                 gNumLights  = 4;
static const uint32_t           gMaxIBLs    = 32;
static uint32_t                 gIBLIndex   = 0;
static std::vector<std::string> gIBLNames   = {};
static uint32_t                 gModelIndex = 0;

void CreatePBRRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateEnvironmentRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    GeometryBuffers& outGeomtryBuffers);
void CreateMaterialModels(
    DxRenderer*                   pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers);
void CreateIBLTextures(
    DxRenderer*                          pRenderer,
    ID3D12Resource**                     ppBRDFLUT,
    ID3D12Resource**                     ppMultiscatterBRDFLUT,
    std::vector<ComPtr<ID3D12Resource>>& outIrradianceTextures,
    std::vector<ComPtr<ID3D12Resource>>& outEnvironmentTextures,
    std::vector<uint32_t>&               outEnvNumLevels);
void CreateMaterials(
    DxRenderer*                      pRenderer,
    MaterialTextures&                outDefaultMaterialTextures,
    std::vector<MaterialTextures>&   outMaterialTexturesSets,
    std::vector<MaterialParameters>& outMaterialParametersSets);
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
        std::string shaderSource = LoadString("projects/253_pbr_material_textures/shaders.hlsl");
        if (shaderSource.empty()) {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

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
        std::string shaderSource = LoadString("projects/253_pbr_material_textures/drawtexture.hlsl");
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
    // Scene buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> sceneBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        Align<size_t>(sizeof(SceneParameters), 256),
        nullptr,
        &sceneBuffer));

    // *************************************************************************
    // Environment vertex buffers
    // *************************************************************************
    GeometryBuffers envGeoBuffers;
    CreateEnvironmentVertexBuffers(
        renderer.get(),
        envGeoBuffers);

    // *************************************************************************
    // Material models
    // *************************************************************************
    std::vector<GeometryBuffers> matGeoBuffers;
    CreateMaterialModels(
        renderer.get(),
        matGeoBuffers);

    // *************************************************************************
    // Environment texture
    // *************************************************************************
    ComPtr<ID3D12Resource>              brdfLUT;
    ComPtr<ID3D12Resource>              multiscatterBRDFLUT;
    std::vector<ComPtr<ID3D12Resource>> irrTextures;
    std::vector<ComPtr<ID3D12Resource>> envTextures;
    std::vector<uint32_t>               envNumLevels;
    CreateIBLTextures(
        renderer.get(),
        &brdfLUT,
        &multiscatterBRDFLUT,
        irrTextures,
        envTextures,
        envNumLevels);

    // *************************************************************************
    // Material texture
    // *************************************************************************
    MaterialTextures                defaultMaterialTextures;
    std::vector<MaterialTextures>   materialTexturesSets;
    std::vector<MaterialParameters> materialParametersSets;
    CreateMaterials(
        renderer.get(),
        defaultMaterialTextures,
        materialTexturesSets,
        materialParametersSets);

    // *************************************************************************
    // Material buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> materialBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        SizeInBytes(materialParametersSets),
        DataPtr(materialParametersSets),
        &materialBuffer));

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    CreateDescriptorHeap(renderer.get(), &descriptorHeap);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE heapStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto                        incSize   = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // IBLIntegrationLUT (t3)
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {heapStart.ptr + IBL_INTEGRATION_LUT_DESCRIPTOR_OFFSET * incSize};
        CreateDescriptorTexture2D(renderer.get(), brdfLUT.Get(), descriptor);

        // IBLIntegrationMultiscatterLUT (t4)
        descriptor = {heapStart.ptr + IBL_INTEGRATION_MS_LUT_DESCRIPTOR_OFFSET * incSize};
        CreateDescriptorTexture2D(renderer.get(), multiscatterBRDFLUT.Get(), descriptor);

        // IBLIrradianceMaps (t16)
        descriptor = {heapStart.ptr + IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET * incSize};
        for (size_t i = 0; i < irrTextures.size(); ++i) {
            CreateDescriptorTexture2D(renderer.get(), irrTextures[i].Get(), descriptor);
            descriptor.ptr += incSize;
        }

        // IBLEnvironmentMaps (t48)
        descriptor = {heapStart.ptr + IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET * incSize};
        for (size_t i = 0; i < irrTextures.size(); ++i) {
            CreateDescriptorTexture2D(renderer.get(), envTextures[i].Get(), descriptor, 0, envNumLevels[i]);
            descriptor.ptr += incSize;
        }

        // Material textures
        descriptor = {heapStart.ptr + MATERIAL_TEXTURES_DESCRIPTOR_OFFSET * incSize};
        for (auto& materialTextures : materialTexturesSets) {
            // Base Color
            CreateDescriptorTexture2D(renderer.get(), materialTextures.baseColorTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Normal
            CreateDescriptorTexture2D(renderer.get(), materialTextures.normalTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Roughness
            CreateDescriptorTexture2D(renderer.get(), materialTextures.roughnessTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // Metallic
            CreateDescriptorTexture2D(renderer.get(), materialTextures.metallicTexture.Get(), descriptor);
            descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "253_pbr_material_textures_d3d12");
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
    // Persistent map parameters
    // *************************************************************************
    SceneParameters* pSceneParams = nullptr;
    CHECK_CALL(sceneBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pSceneParams)));

    MaterialParameters* pMaterialParams = nullptr;
    CHECK_CALL(materialBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pMaterialParams)));

    // *************************************************************************
    // Set some scene params
    // *************************************************************************
    pSceneParams->numLights           = gNumLights;
    pSceneParams->lights[0].active    = 0;
    pSceneParams->lights[0].position  = vec3(3, 10, 0);
    pSceneParams->lights[0].color     = vec3(1, 1, 1);
    pSceneParams->lights[0].intensity = 1.5f;
    pSceneParams->lights[1].active    = 0;
    pSceneParams->lights[1].position  = vec3(-8, 1, 4);
    pSceneParams->lights[1].color     = vec3(0.85f, 0.95f, 0.81f);
    pSceneParams->lights[1].intensity = 0.4f;
    pSceneParams->lights[2].active    = 0;
    pSceneParams->lights[2].position  = vec3(0, 8, -8);
    pSceneParams->lights[2].color     = vec3(0.89f, 0.89f, 0.97f);
    pSceneParams->lights[2].intensity = 0.95f;
    pSceneParams->lights[3].active    = 0;
    pSceneParams->lights[3].position  = vec3(15, 0, 0);
    pSceneParams->lights[3].color     = vec3(0.92f, 0.5f, 0.7f);
    pSceneParams->lights[3].intensity = 0.5f;
    pSceneParams->iblNumEnvLevels     = envNumLevels[gIBLIndex];
    pSceneParams->iblIndex            = gIBLIndex;
    pSceneParams->colorCorrect        = 0;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        window->ImGuiNewFrameD3D12();

        if (ImGui::Begin("Scene")) {
            static const char* currentModelName = gModelNames[0].c_str();
            if (ImGui::BeginCombo("Model", currentModelName)) {
                for (size_t i = 0; i < gModelNames.size(); ++i) {
                    bool isSelected = (currentModelName == gModelNames[i]);
                    if (ImGui::Selectable(gModelNames[i].c_str(), isSelected)) {
                        currentModelName = gModelNames[i].c_str();
                        gModelIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName)) {
                for (size_t i = 0; i < gIBLNames.size(); ++i) {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected)) {
                        currentIBLName         = gIBLNames[i].c_str();
                        pSceneParams->iblIndex = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            ImGui::Checkbox("Multiscatter", reinterpret_cast<bool*>(&pSceneParams->multiscatter));

            ImGui::Separator();

            ImGui::Checkbox("Color Correct", reinterpret_cast<bool*>(&pSceneParams->colorCorrect));

            ImGui::Separator();

            for (uint32_t lightIdx = 0; lightIdx < 4; ++lightIdx) {
                std::stringstream lightName;
                lightName << "Light " << lightIdx;
                if (ImGui::TreeNodeEx(lightName.str().c_str(), ImGuiTreeNodeFlags_None)) {
                    ImGui::Checkbox("Active", reinterpret_cast<bool*>(&pSceneParams->lights[lightIdx].active));
                    ImGui::SliderFloat("Intensity", &pSceneParams->lights[lightIdx].intensity, 0.0f, 10.0f);
                    ImGui::ColorPicker3("Albedo", reinterpret_cast<float*>(&(pSceneParams->lights[lightIdx].color)), ImGuiColorEditFlags_NoInputs);

                    ImGui::TreePop();
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("Material Parameters")) {
            for (uint32_t matIdx = 0; matIdx < gMaterialNames.size(); ++matIdx) {
                if (ImGui::TreeNodeEx(gMaterialNames[matIdx].c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("Specular", &(pMaterialParams[matIdx].specular), 0.0f, 1.0f);

                    ImGui::TreePop();
                }

                ImGui::Separator();
            }
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

        // Draw stuff
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

            // Camera matrices - spin the camera around the target
            mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
            vec3 startingEyePosition = vec3(0, 2.5f, 10);
            vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
            mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

            // Set scene params values that required calculation
            //
            pSceneParams->viewProjectionMatrix = projMat * viewMat;
            pSceneParams->eyePosition          = eyePosition;
            pSceneParams->iblNumEnvLevels      = envNumLevels[gIBLIndex];

            // Draw environment
            {
                commandList->SetGraphicsRootSignature(envRootSig.Get());
                commandList->SetPipelineState(envPipelineState.Get());

                glm::mat4 moveUp = glm::translate(vec3(0, 5, 0));

                // SceneParmas (b0)
                mat4 mvp = projMat * viewMat * moveUp;
                commandList->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);
                commandList->SetGraphicsRoot32BitConstants(0, 1, &pSceneParams->iblIndex, 16);
                // Textures (32)
                D3D12_GPU_DESCRIPTOR_HANDLE tableStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                commandList->SetGraphicsRootDescriptorTable(1, tableStart);

                // Index buffer
                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation          = envGeoBuffers.indexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes             = static_cast<UINT>(envGeoBuffers.indexBuffer->GetDesc().Width);
                ibv.Format                  = DXGI_FORMAT_R32_UINT;
                commandList->IASetIndexBuffer(&ibv);

                // Vertex buffers
                D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
                // Position
                vbvs[0].BufferLocation = envGeoBuffers.positionBuffer->GetGPUVirtualAddress();
                vbvs[0].SizeInBytes    = static_cast<UINT>(envGeoBuffers.positionBuffer->GetDesc().Width);
                vbvs[0].StrideInBytes  = 12;
                // Tex coord
                vbvs[1].BufferLocation = envGeoBuffers.texCoordBuffer->GetGPUVirtualAddress();
                vbvs[1].SizeInBytes    = static_cast<UINT>(envGeoBuffers.texCoordBuffer->GetDesc().Width);
                vbvs[1].StrideInBytes  = 8;

                commandList->IASetVertexBuffers(0, 2, vbvs);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                commandList->DrawIndexedInstanced(envGeoBuffers.numIndices, 1, 0, 0, 0);
            }

            // Draw material models
            {
                const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
                UINT                              incSize   = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                commandList->SetGraphicsRootSignature(pbrRootSig.Get());
                // SceneParams (b0)
                commandList->SetGraphicsRootConstantBufferView(0, sceneBuffer->GetGPUVirtualAddress());
                // MaterialParams (t2)
                commandList->SetGraphicsRootShaderResourceView(2, materialBuffer->GetGPUVirtualAddress());
                // IBL textures (t3, t4)
                D3D12_GPU_DESCRIPTOR_HANDLE tableStart = {heapStart.ptr + IBL_INTEGRATION_LUT_DESCRIPTOR_OFFSET * incSize};
                commandList->SetGraphicsRootDescriptorTable(3, tableStart);
                // IBL irrandiance maps (t16)
                tableStart = {heapStart.ptr + IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET * incSize};
                commandList->SetGraphicsRootDescriptorTable(4, tableStart);
                // IBL environemnt maps (t48)
                tableStart = {heapStart.ptr + IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET * incSize};
                commandList->SetGraphicsRootDescriptorTable(5, tableStart);
                // Material textures (t100)
                tableStart = {heapStart.ptr + MATERIAL_TEXTURES_DESCRIPTOR_OFFSET * incSize};
                commandList->SetGraphicsRootDescriptorTable(6, tableStart);

                // Select which model to draw
                const GeometryBuffers& geoBuffers = matGeoBuffers[gModelIndex];

                // Index buffer
                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation          = geoBuffers.indexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes             = static_cast<UINT>(geoBuffers.indexBuffer->GetDesc().Width);
                ibv.Format                  = DXGI_FORMAT_R32_UINT;
                commandList->IASetIndexBuffer(&ibv);

                // Vertex buffers
                D3D12_VERTEX_BUFFER_VIEW vbvs[5] = {};
                // Position
                vbvs[0].BufferLocation = geoBuffers.positionBuffer->GetGPUVirtualAddress();
                vbvs[0].SizeInBytes    = static_cast<UINT>(geoBuffers.positionBuffer->GetDesc().Width);
                vbvs[0].StrideInBytes  = 12;
                // Tex coord
                vbvs[1].BufferLocation = geoBuffers.texCoordBuffer->GetGPUVirtualAddress();
                vbvs[1].SizeInBytes    = static_cast<UINT>(geoBuffers.texCoordBuffer->GetDesc().Width);
                vbvs[1].StrideInBytes  = 8;
                // Normal
                vbvs[2].BufferLocation = geoBuffers.normalBuffer->GetGPUVirtualAddress();
                vbvs[2].SizeInBytes    = static_cast<UINT>(geoBuffers.normalBuffer->GetDesc().Width);
                vbvs[2].StrideInBytes  = 12;
                // Tangent
                vbvs[3].BufferLocation = geoBuffers.tangentBuffer->GetGPUVirtualAddress();
                vbvs[3].SizeInBytes    = static_cast<UINT>(geoBuffers.tangentBuffer->GetDesc().Width);
                vbvs[3].StrideInBytes  = 12;
                // Bitangent
                vbvs[4].BufferLocation = geoBuffers.bitangentBuffer->GetGPUVirtualAddress();
                vbvs[4].SizeInBytes    = static_cast<UINT>(geoBuffers.bitangentBuffer->GetDesc().Width);
                vbvs[4].StrideInBytes  = 12;

                commandList->IASetVertexBuffers(0, 5, vbvs);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // Pipeline state
                commandList->SetPipelineState(pbrPipelineState.Get());

                const float yPos             = 0.0f;
                uint32_t    materialIndex    = 0;
                uint32_t    invertNormalMapY = false; // Invert if sphere

                // Material 0
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, 4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 1
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, 4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 2
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, 4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 3
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, 4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 4
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, 1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 5
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, 1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 6
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, 1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 7
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, 1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 8
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, -1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 9
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, -1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 10
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, -1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 11
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, -1.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 12
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, -4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 13
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, -4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 14
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, -4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
                }

                // Material 15
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, -4.5f));

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &invertNormalMapY, 17);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1)) {
                        ++materialIndex;
                    }
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
    // IBL LUT textures (t3, t4)
    D3D12_DESCRIPTOR_RANGE iblLUTRange            = {};
    iblLUTRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblLUTRange.NumDescriptors                    = 2;
    iblLUTRange.BaseShaderRegister                = IBL_INTEGRATION_LUT_DESCRIPTOR_OFFSET;
    iblLUTRange.RegisterSpace                     = 0;
    iblLUTRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // IBL irradiance textures (t16)
    D3D12_DESCRIPTOR_RANGE iblIrrRange            = {};
    iblIrrRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblIrrRange.NumDescriptors                    = gMaxIBLs;
    iblIrrRange.BaseShaderRegister                = IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET;
    iblIrrRange.RegisterSpace                     = 0;
    iblIrrRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // IBL environment textures (t48)
    D3D12_DESCRIPTOR_RANGE iblEnvRange            = {};
    iblEnvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblEnvRange.NumDescriptors                    = gMaxIBLs;
    iblEnvRange.BaseShaderRegister                = IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET;
    iblEnvRange.RegisterSpace                     = 0;
    iblEnvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // MaterialTextures (t100)
    D3D12_DESCRIPTOR_RANGE materialRange            = {};
    materialRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialRange.NumDescriptors                    = TOTAL_MATERIAL_TEXTURES;
    materialRange.BaseShaderRegister                = MATERIAL_TEXTURES_DESCRIPTOR_OFFSET;
    materialRange.RegisterSpace                     = 0;
    materialRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[7] = {};
    // SceneParams (b0)
    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // DrawParams (b1)
    rootParameters[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].Constants.Num32BitValues = 18;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.RegisterSpace  = 0;
    rootParameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // MaterialParams (t2)
    rootParameters[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // IBL textures (t3, t4)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &iblLUTRange;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // IBL irradiance textures (t16)
    rootParameters[4].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges   = &iblIrrRange;
    rootParameters[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // // IBL environment textures (t48)
    rootParameters[5].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[5].DescriptorTable.pDescriptorRanges   = &iblEnvRange;
    rootParameters[5].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // MaterialTextures (t100)
    rootParameters[6].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[6].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[6].DescriptorTable.pDescriptorRanges   = &materialRange;
    rootParameters[6].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[4] = {};
    // IBLIntegrationSampler (s32)
    staticSamplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[0].MaxAnisotropy    = 0;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].MinLOD           = 0;
    staticSamplers[0].MaxLOD           = 1;
    staticSamplers[0].ShaderRegister   = 32;
    staticSamplers[0].RegisterSpace    = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // IBLMapSampler (s33)
    staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].MipLODBias       = 0.5f; // D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MinLOD           = 0;
    staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister   = 33;
    staticSamplers[1].RegisterSpace    = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // MaterialSampler (s34)
    staticSamplers[2].Filter           = D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR;
    staticSamplers[2].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[2].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[2].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[2].MaxAnisotropy    = 0;
    staticSamplers[2].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[2].MinLOD           = 0;
    staticSamplers[2].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[2].ShaderRegister   = 34;
    staticSamplers[2].RegisterSpace    = 0;
    staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // MaterialSampler (s35)
    staticSamplers[3].Filter           = D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[3].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[3].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[3].MipLODBias       = 0.5f;
    staticSamplers[3].MaxAnisotropy    = 0;
    staticSamplers[3].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[3].MinLOD           = 1;
    staticSamplers[3].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[3].ShaderRegister   = 35;
    staticSamplers[3].RegisterSpace    = 0;
    staticSamplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 7;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.NumStaticSamplers         = 4;
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
    // Textures (t32)
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = gMaxIBLs;
    range.BaseShaderRegister                = 32;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    // SceneParams (b0)
    rootParameters[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.Num32BitValues = 17;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // Textures (t32)
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

void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    GeometryBuffers& outGeomtryBuffers)
{
    TriMesh mesh = TriMesh::Sphere(25, 64, 64, TriMesh::Options().EnableTexCoords().FaceInside());

    outGeomtryBuffers.numIndices = 3 * mesh.GetNumTriangles();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        &outGeomtryBuffers.indexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        &outGeomtryBuffers.positionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        &outGeomtryBuffers.texCoordBuffer));
}

void CreateMaterialModels(
    DxRenderer*                   pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers)
{
    // Sphere
    {
        TriMesh::Options options = TriMesh::Options().EnableTexCoords().EnableNormals().EnableTangents();

        TriMesh mesh = TriMesh::Sphere(1, 256, 256, options);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Knob
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.invertTexCoordsV = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh)) {
            return;
        }
        mesh.ScaleToFit(1.0f);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Monkey
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/monkey.obj").string(), "", options, &mesh)) {
            return;
        }
        // mesh.ScaleToUnit();

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Cube
    {
        TriMesh mesh = TriMesh::Cube(vec3(2), false, TriMesh::Options().EnableTexCoords().EnableNormals().EnableTangents());

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }
}

void CreateIBLTextures(
    DxRenderer*                          pRenderer,
    ID3D12Resource**                     ppBRDFLUT,
    ID3D12Resource**                     ppMultiscatterBRDFLUT,
    std::vector<ComPtr<ID3D12Resource>>& outIrradianceTextures,
    std::vector<ComPtr<ID3D12Resource>>& outEnvironmentTextures,
    std::vector<uint32_t>&               outEnvNumLevels)
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

    // Multiscatter BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut_ms.hdr"));
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
            ppMultiscatterBRDFLUT));
    }

    auto                               iblDir = GetAssetPath("IBL");
    std::vector<std::filesystem::path> iblFiles;
    for (auto& entry : std::filesystem::directory_iterator(iblDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto path = entry.path();
        auto ext  = path.extension();
        if (ext == ".ibl") {
            path = std::filesystem::relative(path, iblDir.parent_path());
            iblFiles.push_back(path);
        }
    }

    size_t maxEntries = std::min<size_t>(gMaxIBLs, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i) {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl)) {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            assert(false && "IBL maps load failed failed");
            return;
        }

        outEnvNumLevels.push_back(ibl.numLevels);

        // Irradiance
        {
            ComPtr<ID3D12Resource> texture;
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.irradianceMap.GetWidth(),
                ibl.irradianceMap.GetHeight(),
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                ibl.irradianceMap.GetSizeInBytes(),
                ibl.irradianceMap.GetPixels(),
                &texture));
            outIrradianceTextures.push_back(texture);
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

            ComPtr<ID3D12Resource> texture;
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.baseWidth,
                ibl.baseHeight,
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                mipOffsets,
                ibl.environmentMap.GetSizeInBytes(),
                ibl.environmentMap.GetPixels(),
                &texture));
            outEnvironmentTextures.push_back(texture);
        }

        gIBLNames.push_back(iblFile.filename().replace_extension().string());

        GREX_LOG_INFO("Loaded " << iblFile);
    }
}

void CreateMaterials(
    DxRenderer*                      pRenderer,
    MaterialTextures&                outDefaultMaterialTextures,
    std::vector<MaterialTextures>&   outMaterialTexturesSets,
    std::vector<MaterialParameters>& outMaterialParametersSets)
{
    // Default material textures
    {
        PixelRGBA8u purplePixel = {0, 0, 0, 255};
        PixelRGBA8u blackPixel  = {0, 0, 0, 255};
        PixelRGBA8u whitePixel  = {255, 255, 255, 255};

        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &purplePixel, &outDefaultMaterialTextures.baseColorTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.normalTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.roughnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.metallicTexture));
    }

    // Texture directory
    auto                               texturesDir = GetAssetPath("textures");

    // Material files - limit to 16 since there's 16 objects draws
    std::vector<std::filesystem::path> materialFiles = {
        texturesDir / "bark_brown_02" / "material.mat",
        texturesDir / "bark_willow" / "material.mat",
        texturesDir / "brick_4" / "material.mat",
        texturesDir / "castle_brick_02_red" / "material.mat",
        texturesDir / "dark_brick_wall" / "material.mat",
        texturesDir / "factory_wall" / "material.mat",
        texturesDir / "green_metal_rust" / "material.mat",
        texturesDir / "hexagonal_concrete_paving" / "material.mat",
        texturesDir / "metal_grate_rusty" / "material.mat",
        texturesDir / "metal_plate" / "material.mat",
        texturesDir / "mud_cracked_dry_riverbed_002" / "material.mat",
        texturesDir / "pavement_02" / "material.mat",
        texturesDir / "rough_plaster_broken" / "material.mat",
        texturesDir / "rusty_metal_02" / "material.mat",
        texturesDir / "weathered_planks" / "material.mat",
        texturesDir / "wood_table_001" / "material.mat",
    };

    size_t maxEntries = materialFiles.size();
    for (size_t i = 0; i < maxEntries; ++i) {
        auto materialFile = materialFiles[i];

        std::ifstream is = std::ifstream(materialFile.string().c_str());
        if (!is.is_open()) {
            assert(false && "faild to open material file");
        }

        MaterialTextures   materialTextures = outDefaultMaterialTextures;
        MaterialParameters materialParams   = {};

        while (!is.eof()) {
            ComPtr<ID3D12Resource>* pTargetTexture = nullptr;
            std::filesystem::path   textureFile    = "";

            std::string key;
            is >> key;
            if (key == "basecolor") {
                is >> textureFile;
                pTargetTexture = &materialTextures.baseColorTexture;
            }
            else if (key == "normal") {
                is >> textureFile;
                pTargetTexture = &materialTextures.normalTexture;
            }
            else if (key == "roughness") {
                is >> textureFile;
                pTargetTexture = &materialTextures.roughnessTexture;
            }
            else if (key == "metallic") {
                is >> textureFile;
                pTargetTexture = &materialTextures.metallicTexture;
            }
            else if (key == "specular") {
                is >> materialParams.specular;
            }

            if (textureFile.empty()) {
                continue;
            }

            auto cwd    = materialFile.parent_path().filename();
            textureFile = "textures" / cwd / textureFile;

            auto bitmap = LoadImage8u(textureFile);
            if (!bitmap.Empty()) {
                MipmapRGBA8u mipmap = MipmapRGBA8u(
                    bitmap,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_FILTER_MODE_NEAREST);

                std::vector<MipOffset> mipOffsets;
                for (auto& srcOffset : mipmap.GetOffsets()) {
                    MipOffset dstOffset = {};
                    dstOffset.Offset    = srcOffset;
                    dstOffset.RowStride = mipmap.GetRowStride();
                    mipOffsets.push_back(dstOffset);
                }

                CHECK_CALL(CreateTexture(
                    pRenderer,
                    mipmap.GetWidth(0),
                    mipmap.GetHeight(0),
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    mipOffsets,
                    mipmap.GetSizeInBytes(),
                    mipmap.GetPixels(),
                    &(*pTargetTexture)));

                GREX_LOG_INFO("Created texture from " << textureFile);
            }
            else {
                GREX_LOG_ERROR("Failed to load: " << textureFile);
                assert(false && "Failed to load texture!");
            }
        }

        outMaterialTexturesSets.push_back(materialTextures);
        outMaterialParametersSets.push_back(materialParams);

        // Use directory name for material name
        gMaterialNames.push_back(materialFile.parent_path().filename().string());
    }
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
