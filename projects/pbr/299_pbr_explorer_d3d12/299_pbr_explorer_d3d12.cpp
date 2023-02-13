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

#define DISTRIBUTION_TROWBRIDGE_REITZ 0
#define DISTRIBUTION_BECKMANN         1
#define DISTRIBUTION_BLINN_PHONG      2

#define FRESNEL_SCHLICK_ROUGHNESS 0
#define FRESNEL_SCHLICK           1
#define FRESNEL_COOK_TORRANCE     2
#define FRESNEL_NONE              3

#define GEOMETRY_SMITHS        0
#define GEOMETRY_IMPLICIT      1
#define GEOMETRY_NEUMANN       2
#define GEOMETRY_COOK_TORRANCE 3
#define GEOMETRY_KELEMEN       4
#define GEOMETRY_BECKMANN      5
#define GEOMETRY_GGX1          6
#define GEOMETRY_GGX2          7
#define GEOMETRY_SCHLICK_GGX   8

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
    uint32_t iblNumEnvLevels;
    uint32_t iblIndex;
    float    iblDiffuseStrength;
    float    iblSpecularStrength;
};

struct MaterialParameters
{
    vec3     albedo;
    float    roughness;
    float    metalness;
    vec3     F0;
    uint32_t D_Func;
    uint32_t F_Func;
    uint32_t G_Func;
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

const std::vector<std::string> gDistributionNames = {
    "GGX (Trowbridge-Reitz)",
    "Beckmann",
    "Blinn-Phong",
};

const std::vector<std::string> gFresnelNames = {
    "Schlick with Roughness",
    "Schlick",
    "CookTorrance",
    "None",
};

const std::vector<std::string> gGeometryNames = {
    "Smiths",
    "Implicit",
    "Neumann",
    "Cook-Torrance",
    "Kelemen",
    "Beckmann",
    "GGX1",
    "GGX2",
    "SchlickGGX",
};

const std::vector<std::string> gModelNames = {
    "Sphere",
    "Knob",
    "Monkey",
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

// clang-format off
static std::vector<MaterialParameters> gMaterialParams = {
    {F0_MetalCopper,         0.25f, 1.00f, F0_MetalCopper    , 0, 0, 0},
    {F0_MetalGold,           0.05f, 1.00f, F0_MetalGold      , 0, 0, 0},
    {F0_MetalSilver,         0.18f, 1.00f, F0_MetalSilver    , 0, 0, 0},
    {F0_MetalZinc,           0.65f, 1.00f, F0_MetalZinc      , 0, 0, 0},
    {F0_MetalTitanium,       0.11f, 1.00f, F0_MetalTitanium  , 0, 0, 0},
    {vec3(0.6f, 0.0f, 0.0f), 0.00f, 0.00f, F0_DiletricPlastic, 0, 0, 0},
    {vec3(0.0f, 0.6f, 0.0f), 0.25f, 0.00f, F0_DiletricPlastic, 0, 0, 0},
    {vec3(0.0f, 0.0f, 0.6f), 0.50f, 0.00f, F0_DiletricPlastic, 0, 0, 0},
    {vec3(0.7f, 0.7f, 0.2f), 0.92f, 0.15f, F0_DiletricPlastic, 0, 0, 0},
};
// clang-format on

static std::vector<std::string> gMaterialNames = {
    "Copper",
    "Gold",
    "Silver",
    "Zink",
    "Titanium",
    "Shiny Plastic",
    "Rough Plastic",
    "Rougher Plastic",
    "Roughest Plastic",
};

static uint32_t                 gNumLights           = 0;
static const uint32_t           gMaxIBLs             = 32;
static uint32_t                 gIBLIndex            = 0;
static std::vector<std::string> gIBLNames            = {};
static float                    gIBLDiffuseStrength  = 1.0f;
static float                    gIBLSpecularStrength = 1.0f;
static uint32_t                 gModelIndex          = 0;

void CreatePBRRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateEnvironmentRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    GeometryBuffers& outGeomtryBuffers);
void CreateMaterialModels(
    DxRenderer*                   pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers);
// void CreateMaterialKnobVertexBuffers(
//     DxRenderer*                   pRenderer,
//     std::vector<GeometryBuffers>& outGeomtryBuffers);
void CreateIBLTextures(
    DxRenderer*                          pRenderer,
    ID3D12Resource**                     ppBRDFLUT,
    std::vector<ComPtr<ID3D12Resource>>& outIrradianceTextures,
    std::vector<ComPtr<ID3D12Resource>>& outEnvironmentTextures,
    uint32_t*                            pEnvNumLevels);
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
        std::string shaderSource = LoadString("projects/299_pbr_explorer_d3d12/shaders.hlsl");
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
        std::string shaderSource = LoadString("projects/299_pbr_explorer_d3d12/drawtexture.hlsl");
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
    // Material buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> materialBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        SizeInBytes(gMaterialParams),
        DataPtr(gMaterialParams),
        &materialBuffer));

    // *************************************************************************
    // Constant buffers
    // *************************************************************************
    ComPtr<ID3D12Resource> constantBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        Align<size_t>(sizeof(SceneParameters), 256),
        nullptr,
        &constantBuffer));

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

    //// *************************************************************************
    //// Material knob vertex buffers
    //// *************************************************************************
    // CreateMaterialKnobVertexBuffers(
    //     renderer.get(),
    //     matGeoBuffers);

    // *************************************************************************
    // Environment texture
    // *************************************************************************
    ComPtr<ID3D12Resource>              brdfLUT;
    std::vector<ComPtr<ID3D12Resource>> irrTextures;
    std::vector<ComPtr<ID3D12Resource>> envTextures;
    uint32_t                            envNumLevels = 0;
    CreateIBLTextures(renderer.get(), &brdfLUT, irrTextures, envTextures, &envNumLevels);

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    CreateDescriptorHeap(renderer.get(), &descriptorHeap);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE heapStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto                        incSize   = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // LUT
        CreateDescriptorTexture2D(renderer.get(), brdfLUT.Get(), heapStart);

        // Irradiance
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {heapStart.ptr + incSize};
        for (size_t i = 0; i < irrTextures.size(); ++i) {
            CreateDescriptorTexture2D(renderer.get(), irrTextures[i].Get(), descriptor);
            descriptor.ptr += incSize;
        }

        // Environment
        descriptor = {heapStart.ptr + (1 + gMaxIBLs) * incSize};
        for (size_t i = 0; i < irrTextures.size(); ++i) {
            CreateDescriptorTexture2D(renderer.get(), envTextures[i].Get(), descriptor, 0, envNumLevels);
            descriptor.ptr += incSize;
        }
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "299_pbr_explorer_d3d12");
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
    CHECK_CALL(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pSceneParams)));

    MaterialParameters* pMaterialParams = nullptr;
    CHECK_CALL(materialBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pMaterialParams)));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        window->ImGuiNewFrameD3D12();

        if (ImGui::Begin("Scene")) {
            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName)) {
                for (size_t i = 0; i < gIBLNames.size(); ++i) {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected)) {
                        currentIBLName = gIBLNames[i].c_str();
                        gIBLIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SliderFloat("IBL Diffuse Strength", &gIBLDiffuseStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("IBL Specular Strength", &gIBLSpecularStrength, 0.0f, 2.0f);
            ImGui::SliderInt("Number of Lights", reinterpret_cast<int*>(&gNumLights), 0, 4);

            ImGui::Separator();

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
        }
        ImGui::End();

        if (ImGui::Begin("Material Parameters")) {
            static std::vector<const char*> currentDistributionNames(gMaterialParams.size(), gDistributionNames[0].c_str());
            static std::vector<const char*> currentFresnelNames(gMaterialParams.size(), gFresnelNames[0].c_str());
            static std::vector<const char*> currentGeometryNames(gMaterialParams.size(), gGeometryNames[0].c_str());

            for (uint32_t matIdx = 0; matIdx < gMaterialNames.size(); ++matIdx) {
                if (ImGui::TreeNodeEx(gMaterialNames[matIdx].c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    // Distribution
                    if (ImGui::BeginCombo("Distribution", currentDistributionNames[matIdx])) {
                        for (size_t i = 0; i < gDistributionNames.size(); ++i) {
                            bool isSelected = (currentDistributionNames[matIdx] == gDistributionNames[i]);
                            if (ImGui::Selectable(gDistributionNames[i].c_str(), isSelected)) {
                                currentDistributionNames[matIdx] = gDistributionNames[i].c_str();
                                pMaterialParams[matIdx].D_Func   = static_cast<uint32_t>(i);
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    // Fresnel
                    if (ImGui::BeginCombo("Fresnel", currentFresnelNames[matIdx])) {
                        for (size_t i = 0; i < gFresnelNames.size(); ++i) {
                            bool isSelected = (currentFresnelNames[matIdx] == gFresnelNames[i]);
                            if (ImGui::Selectable(gFresnelNames[i].c_str(), isSelected)) {
                                currentFresnelNames[matIdx]    = gFresnelNames[i].c_str();
                                pMaterialParams[matIdx].F_Func = static_cast<uint32_t>(i);
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    // Geometry
                    if (ImGui::BeginCombo("Geometry", currentGeometryNames[matIdx])) {
                        for (size_t i = 0; i < gGeometryNames.size(); ++i) {
                            bool isSelected = (currentGeometryNames[matIdx] == gGeometryNames[i]);
                            if (ImGui::Selectable(gGeometryNames[i].c_str(), isSelected)) {
                                currentGeometryNames[matIdx]   = gGeometryNames[i].c_str();
                                pMaterialParams[matIdx].G_Func = static_cast<uint32_t>(i);
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SliderFloat("Roughness", &(pMaterialParams[matIdx].roughness), 0.0f, 1.0f);
                    ImGui::SliderFloat("Metalness", &(pMaterialParams[matIdx].metalness), 0.0f, 1.0f);
                    ImGui::ColorPicker3("Albedo", reinterpret_cast<float*>(&(pMaterialParams[matIdx].albedo)), ImGuiColorEditFlags_NoInputs);

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
            vec3 startingEyePosition = vec3(0, 3, 8);
            vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
            mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

            // Set constant buffer values
            //
            // We're rotating everything in the world...including the lights
            //
            pSceneParams->viewProjectionMatrix = projMat * viewMat;
            pSceneParams->eyePosition          = eyePosition;
            pSceneParams->numLights            = gNumLights;
            pSceneParams->lights[0].position   = vec3(3, 10, 0);
            pSceneParams->lights[0].color      = vec3(1, 1, 1);
            pSceneParams->lights[0].intensity  = 1.5f;
            pSceneParams->lights[1].position   = vec3(-8, 1, 4);
            pSceneParams->lights[1].color      = vec3(0.85f, 0.95f, 0.81f);
            pSceneParams->lights[1].intensity  = 0.4f;
            pSceneParams->lights[2].position   = vec3(0, 8, -8);
            pSceneParams->lights[2].color      = vec3(0.89f, 0.89f, 0.97f);
            pSceneParams->lights[2].intensity  = 0.95f;
            pSceneParams->lights[3].position   = vec3(15, 0, 0);
            pSceneParams->lights[3].color      = vec3(0.92f, 0.5f, 0.7f);
            pSceneParams->lights[3].intensity  = 0.5f;
            pSceneParams->iblNumEnvLevels      = envNumLevels;
            pSceneParams->iblIndex             = gIBLIndex;
            pSceneParams->iblDiffuseStrength   = gIBLDiffuseStrength;
            pSceneParams->iblSpecularStrength  = gIBLSpecularStrength;

            // Draw environment
            {
                commandList->SetGraphicsRootSignature(envRootSig.Get());
                commandList->SetPipelineState(envPipelineState.Get());

                glm::mat4 moveUp = glm::translate(vec3(0, 5, 0));

                // SceneParmas (b0)
                mat4 mvp = projMat * viewMat * moveUp;
                commandList->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);
                commandList->SetGraphicsRoot32BitConstants(0, 1, &gIBLIndex, 16);
                // Textures (32)
                D3D12_GPU_DESCRIPTOR_HANDLE tableStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += (1 + gMaxIBLs) * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

            // Draw sample spheres
            {
                commandList->SetGraphicsRootSignature(pbrRootSig.Get());
                // SceneParams (b0)
                commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
                // MaterialParams (b2)
                commandList->SetGraphicsRootShaderResourceView(2, materialBuffer->GetGPUVirtualAddress());
                // BRDFLUT (t10)
                D3D12_GPU_DESCRIPTOR_HANDLE tableStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
                commandList->SetGraphicsRootDescriptorTable(3, tableStart);
                tableStart.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                // IrradianceMap (t16)
                commandList->SetGraphicsRootDescriptorTable(4, tableStart);
                tableStart.ptr += gMaxIBLs * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                // EnvironmentMap (t32)
                commandList->SetGraphicsRootDescriptorTable(5, tableStart);
                tableStart.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                // Select which model to draw
                const GeometryBuffers& geoBuffers = matGeoBuffers[gModelIndex];

                // Index buffer
                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation          = geoBuffers.indexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes             = static_cast<UINT>(geoBuffers.indexBuffer->GetDesc().Width);
                ibv.Format                  = DXGI_FORMAT_R32_UINT;
                commandList->IASetIndexBuffer(&ibv);

                // Vertex buffers
                D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
                // Position
                vbvs[0].BufferLocation = geoBuffers.positionBuffer->GetGPUVirtualAddress();
                vbvs[0].SizeInBytes    = static_cast<UINT>(geoBuffers.positionBuffer->GetDesc().Width);
                vbvs[0].StrideInBytes  = 12;
                // Normal
                vbvs[1].BufferLocation = geoBuffers.normalBuffer->GetGPUVirtualAddress();
                vbvs[1].SizeInBytes    = static_cast<UINT>(geoBuffers.normalBuffer->GetDesc().Width);
                vbvs[1].StrideInBytes  = 12;

                commandList->IASetVertexBuffers(0, 2, vbvs);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // Pipeline state
                commandList->SetPipelineState(pbrPipelineState.Get());

                const float yPos = 0.0f;

                // Copper
                {
                    glm::mat4 modelMat      = glm::translate(vec3(-3, yPos, 3));
                    uint32_t  materialIndex = 0;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Gold
                {
                    glm::mat4 modelMat      = glm::translate(vec3(0, yPos, 3));
                    uint32_t  materialIndex = 1;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Silver
                {
                    glm::mat4 modelMat      = glm::translate(vec3(3, yPos, 3));
                    uint32_t  materialIndex = 2;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Zink
                {
                    glm::mat4 modelMat      = glm::translate(vec3(-3, yPos, 0));
                    uint32_t  materialIndex = 3;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Titanium
                {
                    glm::mat4 modelMat      = glm::translate(vec3(0, yPos, 0));
                    uint32_t  materialIndex = 4;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Shiny Plastic
                {
                    glm::mat4 modelMat      = glm::translate(vec3(3, yPos, 0));
                    uint32_t  materialIndex = 5;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Rough Plastic
                {
                    glm::mat4 modelMat      = glm::translate(vec3(-3, yPos, -3));
                    uint32_t  materialIndex = 6;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Rougher Plastic
                {
                    glm::mat4 modelMat      = glm::translate(vec3(0, yPos, -3));
                    uint32_t  materialIndex = 7;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
                }

                // Roughest Plastic
                {
                    glm::mat4 modelMat      = glm::translate(vec3(3, yPos, -3));
                    uint32_t  materialIndex = 8;

                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    commandList->SetGraphicsRoot32BitConstants(1, 1, &materialIndex, 16);
                    commandList->DrawIndexedInstanced(geoBuffers.numIndices, 1, 0, 0, 0);
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
    // BRDFLUT (t10)
    D3D12_DESCRIPTOR_RANGE range1            = {};
    range1.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors                    = 1;
    range1.BaseShaderRegister                = 10;
    range1.RegisterSpace                     = 0;
    range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // IrradianceMap (t16)
    D3D12_DESCRIPTOR_RANGE range2            = {};
    range2.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range2.NumDescriptors                    = gMaxIBLs;
    range2.BaseShaderRegister                = 16;
    range2.RegisterSpace                     = 0;
    range2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // EnvironmentMap (t32)
    D3D12_DESCRIPTOR_RANGE range3            = {};
    range3.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range3.NumDescriptors                    = gMaxIBLs;
    range3.BaseShaderRegister                = 48;
    range3.RegisterSpace                     = 0;
    range3.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[6] = {};
    // SceneParams (b0)
    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // DrawParams (b1)
    rootParameters[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].Constants.Num32BitValues = 36;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.RegisterSpace  = 0;
    rootParameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // MaterialParams (t2)
    rootParameters[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // BRDFLUT (t10)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &range1;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // IrradianceMap (t16)
    rootParameters[4].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges   = &range2;
    rootParameters[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // EnvironmentMap (t32)
    rootParameters[5].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[5].DescriptorTable.pDescriptorRanges   = &range3;
    rootParameters[5].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    // ClampedSampler (s4)
    staticSamplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[0].MaxAnisotropy    = 0;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].MinLOD           = 0;
    staticSamplers[0].MaxLOD           = 1;
    staticSamplers[0].ShaderRegister   = 4;
    staticSamplers[0].RegisterSpace    = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // UWrapSampler (s5)
    staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MinLOD           = 0;
    staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister   = 5;
    staticSamplers[1].RegisterSpace    = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 6;
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
    TriMesh mesh = TriMesh::Sphere(25, 64, 64, {.enableTexCoords = true, .faceInside = true});

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
        TriMesh mesh = TriMesh::Sphere(1, 256, 256, {.enableNormals = true});

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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Knob
    {
        TriMesh::Options options = {};
        options.enableNormals    = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh)) {
            return;
        }
        mesh.ScaleToUnit();

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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Monkey
    {
        TriMesh::Options options = {};
        options.enableNormals    = true;
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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }
}

void CreateIBLTextures(
    DxRenderer*                          pRenderer,
    ID3D12Resource**                     ppBRDFLUT,
    std::vector<ComPtr<ID3D12Resource>>& outIrradianceTextures,
    std::vector<ComPtr<ID3D12Resource>>& outEnvironmentTextures,
    uint32_t*                            pEnvNumLevels)
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
        auto& iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl)) {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            return;
        }

        *pEnvNumLevels = ibl.numLevels;

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