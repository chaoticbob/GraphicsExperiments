#include "window.h"

#include "dx_renderer.h"

#include "dx_faux_render.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr))                              \
        {                                            \
            std::stringstream ss;                    \
            ss << "\n";                              \
            ss << "*** FUNCTION CALL FAILED *** \n"; \
            ss << "FUNCTION: " << #FN << "\n";       \
            ss << "\n";                              \
            GREX_LOG_ERROR(ss.str().c_str());        \
            assert(false);                           \
        }                                            \
    }

#define SCENE_REGISTER                     0   // b0
#define CAMERA_REGISTER                    1   // b1
#define DRAW_REGISTER                      2   // b2
#define INSTANCE_BUFFER_REGISTER           10  // t10
#define MATERIAL_BUFFER_REGISTER           11  // t11
#define MATERIAL_SAMPLER_START_REGISTER    100 // s100
#define MATERIAL_IMAGES_START_REGISTER     200 // t200
#define IBL_ENV_MAP_TEXTURE_START_REGISTER 32  // t32
#define IBL_IRR_MAP_TEXTURE_START_REGISTER 64  // t64
#define IBL_INTEGRATION_LUT_REGISTER       16  // t16
#define IBL_MAP_SAMPLER_REGISTER           18  // s18
#define IBL_INTEGRATION_SAMPLER_REGISTER   19  // s19

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

static const uint32_t gNumIBLLUTs            = 2;
static const uint32_t gNumIBLTextures        = 1;
static const uint32_t gNumIBLEnvTextures     = gNumIBLTextures;
static const uint32_t gNumIBLIrrTextures     = gNumIBLTextures;
static const uint32_t gIBLLUTsOffset         = 0;
static const uint32_t gIBLEnvTextureOffset   = gIBLLUTsOffset + gNumIBLLUTs;
static const uint32_t gIBLIrrTextureOffset   = gIBLEnvTextureOffset + gNumIBLEnvTextures;
static const uint32_t gMaterialTextureOffset = gIBLIrrTextureOffset + gNumIBLIrrTextures;

static std::vector<std::string> gIBLNames = {};

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

void CreateGlobalRootSig(DxRenderer* pRenderer, DxFauxRender::SceneGraph* pSceneGraph, ID3D12RootSignature** ppRootSig);
void CreateDescriptorHeaps(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppCBVSRVUAVHeap, ID3D12DescriptorHeap** ppSamplerHeap);
void CreateIBLTextures(
    DxRenderer*                          pRenderer,
    ID3D12Resource**                     ppBRDFLUT,
    ID3D12Resource**                     ppMultiscatterBRDFLUT,
    std::vector<ComPtr<ID3D12Resource>>& outIrradianceTextures,
    std::vector<ComPtr<ID3D12Resource>>& outEnvironmentTextures,
    std::vector<uint32_t>&               outEnvNumLevels);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT)
    {
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

    if (!InitDx(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<char> dxilVS;
    std::vector<char> dxilPS;
    {
        std::string shaderSource = LoadString("faux_render_shaders/render_pbr_material.hlsl");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &dxilVS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &dxilPS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Scene
    // *************************************************************************
    DxFauxRender::SceneGraph graph = DxFauxRender::SceneGraph(renderer.get());
    if (!FauxRender::LoadGLTF(GetAssetPath("scenes/material_test_001_png/material_test_001.gltf"), {}, &graph))
    {
        assert(false && "LoadGLTF failed");
        return EXIT_FAILURE;
    }
    if (!graph.InitializeResources())
    {
        assert(false && "Graph resources initialization failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature> rootSig;
    CreateGlobalRootSig(renderer.get(), &graph, &rootSig);

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    ComPtr<ID3D12PipelineState> pipelineState;
    CHECK_CALL(CreateGraphicsPipeline2(
        renderer.get(),
        rootSig.Get(),
        dxilVS,
        dxilPS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipelineState));

    // *************************************************************************
    // IBL textures
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
    // DescriptorHeap
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> cbvsrvuavHeap;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    CreateDescriptorHeaps(renderer.get(), &cbvsrvuavHeap, &samplerHeap);
    {
        const auto cbvsrvuavHeapStart = cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart();
        const auto cbvsrvuavInc       = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const auto samplerHeapStart = samplerHeap->GetCPUDescriptorHandleForHeapStart();
        const auto samplerInc       = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        // IBL Textures
        {
            // LUT
            {
                auto descriptorHandle = D3D12_CPU_DESCRIPTOR_HANDLE{cbvsrvuavHeapStart.ptr + (gIBLLUTsOffset * cbvsrvuavInc)};

                // Write texture descriptor
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                          = brdfLUT->GetDesc().Format;
                srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MostDetailedMip       = 0;
                srvDesc.Texture2D.MipLevels             = brdfLUT->GetDesc().MipLevels;
                srvDesc.Texture2D.PlaneSlice            = 0;
                srvDesc.Texture2D.ResourceMinLODClamp   = 0;

                renderer->Device->CreateShaderResourceView(brdfLUT.Get(), &srvDesc, descriptorHandle);
            }

            // Environment textures
            for (size_t i = 0; i < envTextures.size(); ++i)
            {
                auto& resource = envTextures[i];

                auto descriptorHandle = D3D12_CPU_DESCRIPTOR_HANDLE{cbvsrvuavHeapStart.ptr + ((gIBLEnvTextureOffset + i) * cbvsrvuavInc)};

                // Write texture descriptor
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                          = resource->GetDesc().Format;
                srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MostDetailedMip       = 0;
                srvDesc.Texture2D.MipLevels             = resource->GetDesc().MipLevels;
                srvDesc.Texture2D.PlaneSlice            = 0;
                srvDesc.Texture2D.ResourceMinLODClamp   = 0;

                renderer->Device->CreateShaderResourceView(resource.Get(), &srvDesc, descriptorHandle);
            }

            // Irradiance textures
            for (size_t i = 0; i < irrTextures.size(); ++i)
            {
                auto& resource = irrTextures[i];

                auto descriptorHandle = D3D12_CPU_DESCRIPTOR_HANDLE{cbvsrvuavHeapStart.ptr + ((gIBLIrrTextureOffset + i) * cbvsrvuavInc)};

                // Write texture descriptor
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                          = resource->GetDesc().Format;
                srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MostDetailedMip       = 0;
                srvDesc.Texture2D.MipLevels             = resource->GetDesc().MipLevels;
                srvDesc.Texture2D.PlaneSlice            = 0;
                srvDesc.Texture2D.ResourceMinLODClamp   = 0;

                renderer->Device->CreateShaderResourceView(resource.Get(), &srvDesc, descriptorHandle);
            }
        }

        // Material Textures
        {
            for (size_t i = 0; i < graph.Images.size(); ++i)
            {
                auto image    = DxFauxRender::Cast(graph.Images[i].get());
                auto resource = image->Resource;

                auto descriptorHandle = D3D12_CPU_DESCRIPTOR_HANDLE{cbvsrvuavHeapStart.ptr + ((gMaterialTextureOffset + i) * cbvsrvuavInc)};

                // Write texture descriptor
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                          = resource->GetDesc().Format;
                srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MostDetailedMip       = 0;
                srvDesc.Texture2D.MipLevels             = image->NumLevels;
                srvDesc.Texture2D.PlaneSlice            = 0;
                srvDesc.Texture2D.ResourceMinLODClamp   = 0;

                renderer->Device->CreateShaderResourceView(resource.Get(), &srvDesc, descriptorHandle);
            }
        }

        // Material Samplers
        {
            auto descriptorHandle = samplerHeap->GetCPUDescriptorHandleForHeapStart();

            // Clamped
            {
                // Write sampler descriptor
                D3D12_SAMPLER_DESC samplerDesc = {};
                samplerDesc.Filter             = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                samplerDesc.AddressU           = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplerDesc.AddressV           = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplerDesc.AddressW           = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplerDesc.MipLODBias         = D3D12_DEFAULT_MIP_LOD_BIAS;
                samplerDesc.MaxAnisotropy      = 0;
                samplerDesc.ComparisonFunc     = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                samplerDesc.MinLOD             = 0;
                samplerDesc.MaxLOD             = 1;

                renderer->Device->CreateSampler(&samplerDesc, descriptorHandle);
            }

            descriptorHandle.ptr += samplerInc;

            // Repeat
            {
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

                renderer->Device->CreateSampler(&samplerDesc, descriptorHandle);
            }
        }
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "405_gltf_full_material_test_d3d12");
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT))
    {
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
    while (window->PollEvents())
    {
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

            // Viewport and scissor
            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

            // Descriptor heaps
            ID3D12DescriptorHeap* heaps[2] = {cbvsrvuavHeap.Get(), samplerHeap.Get()};
            commandList->SetDescriptorHeaps(2, heaps);

            // Root sig
            commandList->SetGraphicsRootSignature(rootSig.Get());
            // Pipeline
            commandList->SetPipelineState(pipelineState.Get());

            // Scene
            {
                uint32_t iblEnvironmentNumLevels = envTextures[0]->GetDesc().MipLevels;

                commandList->SetGraphicsRoot32BitConstants(
                    graph.RootParameterIndices.Scene,
                    1,
                    &iblEnvironmentNumLevels,
                    0);
            }

            // Material samplers
            {
                commandList->SetGraphicsRootDescriptorTable(
                    graph.RootParameterIndices.MaterialSampler,
                    samplerHeap->GetGPUDescriptorHandleForHeapStart());
            }
            // Material textures
            {
                auto tableStart = cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += gMaterialTextureOffset * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                commandList->SetGraphicsRootDescriptorTable(
                    graph.RootParameterIndices.MaterialImages,
                    tableStart);
            }
            // IBL integration LUT
            {
                auto tableStart = cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += gIBLLUTsOffset * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                commandList->SetGraphicsRootDescriptorTable(
                    graph.RootParameterIndices.IBLIntegrationLUT,
                    tableStart);
            }
            // IBL environment textures
            {
                auto tableStart = cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += gIBLEnvTextureOffset * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                commandList->SetGraphicsRootDescriptorTable(
                    graph.RootParameterIndices.IBLEnvMapTexture,
                    tableStart);
            }
            // IBL irradiance textures
            {
                auto tableStart = cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart();
                tableStart.ptr += gIBLIrrTextureOffset * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                commandList->SetGraphicsRootDescriptorTable(
                    graph.RootParameterIndices.IBLIrrMapTexture,
                    tableStart);
            }

            // Topology
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Draw scene
            const auto& scene = graph.Scenes[0];
            DxFauxRender::Draw(&graph, scene.get(), commandList.Get());
        }
        D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &postRenderBarrier);

        commandList->Close();

        ID3D12CommandList* pList = commandList.Get();
        renderer->Queue->ExecuteCommandLists(1, &pList);

        if (!WaitForGpu(renderer.get()))
        {
            assert(false && "WaitForGpu failed");
            break;
        }

        // Present
        if (!SwapchainPresent(renderer.get()))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateGlobalRootSig(DxRenderer* pRenderer, DxFauxRender::SceneGraph* pSceneGraph, ID3D12RootSignature** ppRootSig)
{
    // IBL LUTs
    D3D12_DESCRIPTOR_RANGE iblLUTRange            = {};
    iblLUTRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblLUTRange.NumDescriptors                    = 2;
    iblLUTRange.BaseShaderRegister                = IBL_INTEGRATION_LUT_REGISTER;
    iblLUTRange.RegisterSpace                     = 0;
    iblLUTRange.OffsetInDescriptorsFromTableStart = 0;

    // IBL environment textures
    D3D12_DESCRIPTOR_RANGE iblEnvTextureRange            = {};
    iblEnvTextureRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblEnvTextureRange.NumDescriptors                    = gNumIBLEnvTextures;
    iblEnvTextureRange.BaseShaderRegister                = IBL_ENV_MAP_TEXTURE_START_REGISTER;
    iblEnvTextureRange.RegisterSpace                     = 0;
    iblEnvTextureRange.OffsetInDescriptorsFromTableStart = 0;

    // IBL irradiance textures
    D3D12_DESCRIPTOR_RANGE iblIrrTextureRange            = {};
    iblIrrTextureRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblIrrTextureRange.NumDescriptors                    = gNumIBLIrrTextures;
    iblIrrTextureRange.BaseShaderRegister                = IBL_IRR_MAP_TEXTURE_START_REGISTER;
    iblIrrTextureRange.RegisterSpace                     = 0;
    iblIrrTextureRange.OffsetInDescriptorsFromTableStart = 0;

    // Material samplers
    D3D12_DESCRIPTOR_RANGE materialSamplerRange            = {};
    materialSamplerRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    materialSamplerRange.NumDescriptors                    = FauxRender::Shader::MAX_SAMPLERS;
    materialSamplerRange.BaseShaderRegister                = MATERIAL_SAMPLER_START_REGISTER;
    materialSamplerRange.RegisterSpace                     = 0;
    materialSamplerRange.OffsetInDescriptorsFromTableStart = 0;

    // Material textures
    D3D12_DESCRIPTOR_RANGE materialTextureRange            = {};
    materialTextureRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialTextureRange.NumDescriptors                    = FauxRender::Shader::MAX_IMAGES;
    materialTextureRange.BaseShaderRegister                = MATERIAL_IMAGES_START_REGISTER;
    materialTextureRange.RegisterSpace                     = 0;
    materialTextureRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParameters[10] = {};
    // Scene
    uint32* pIndex                                   = &pSceneGraph->RootParameterIndices.Scene;
    *pIndex                                          = 0;
    rootParameters[*pIndex].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[*pIndex].Constants.ShaderRegister = SCENE_REGISTER;
    rootParameters[*pIndex].Constants.RegisterSpace  = 0;
    rootParameters[*pIndex].Constants.Num32BitValues = 1;
    rootParameters[*pIndex].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // Camera
    pIndex                                            = &pSceneGraph->RootParameterIndices.Camera;
    *pIndex                                           = 1;
    rootParameters[*pIndex].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[*pIndex].Descriptor.ShaderRegister = CAMERA_REGISTER;
    rootParameters[*pIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[*pIndex].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // Draw
    pIndex                                           = &pSceneGraph->RootParameterIndices.Draw;
    *pIndex                                          = 2;
    rootParameters[*pIndex].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[*pIndex].Constants.ShaderRegister = DRAW_REGISTER;
    rootParameters[*pIndex].Constants.RegisterSpace  = 0;
    rootParameters[*pIndex].Constants.Num32BitValues = 2;
    rootParameters[*pIndex].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // Instances
    pIndex                                            = &pSceneGraph->RootParameterIndices.InstanceBuffer;
    *pIndex                                           = 3;
    rootParameters[*pIndex].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[*pIndex].Descriptor.ShaderRegister = INSTANCE_BUFFER_REGISTER;
    rootParameters[*pIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[*pIndex].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // Materials
    pIndex                                            = &pSceneGraph->RootParameterIndices.MaterialBuffer;
    *pIndex                                           = 4;
    rootParameters[*pIndex].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[*pIndex].Descriptor.ShaderRegister = MATERIAL_BUFFER_REGISTER;
    rootParameters[*pIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[*pIndex].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // Material samplers
    pIndex                                                      = &pSceneGraph->RootParameterIndices.MaterialSampler;
    *pIndex                                                     = 5;
    rootParameters[*pIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[*pIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[*pIndex].DescriptorTable.pDescriptorRanges   = &materialSamplerRange;
    rootParameters[*pIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // Material textures
    pIndex                                                      = &pSceneGraph->RootParameterIndices.MaterialImages;
    *pIndex                                                     = 6;
    rootParameters[*pIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[*pIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[*pIndex].DescriptorTable.pDescriptorRanges   = &materialTextureRange;
    rootParameters[*pIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // IBL LUTs
    pIndex                                                      = &pSceneGraph->RootParameterIndices.IBLIntegrationLUT;
    *pIndex                                                     = 7;
    rootParameters[*pIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[*pIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[*pIndex].DescriptorTable.pDescriptorRanges   = &iblLUTRange;
    rootParameters[*pIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // IBL environment textures
    pIndex                                                      = &pSceneGraph->RootParameterIndices.IBLEnvMapTexture;
    *pIndex                                                     = 8;
    rootParameters[*pIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[*pIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[*pIndex].DescriptorTable.pDescriptorRanges   = &iblEnvTextureRange;
    rootParameters[*pIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // IBL irradiance textures
    pIndex                                                      = &pSceneGraph->RootParameterIndices.IBLIrrMapTexture;
    *pIndex                                                     = 9;
    rootParameters[*pIndex].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[*pIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[*pIndex].DescriptorTable.pDescriptorRanges   = &iblIrrTextureRange;
    rootParameters[*pIndex].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    // IBL map sampler
    staticSamplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[0].MaxAnisotropy    = 0;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].MinLOD           = 0;
    staticSamplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister   = IBL_MAP_SAMPLER_REGISTER;
    staticSamplers[0].RegisterSpace    = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // IBL integration sampler
    staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MinLOD           = 0;
    staticSamplers[1].MaxLOD           = 1;
    staticSamplers[1].ShaderRegister   = IBL_INTEGRATION_SAMPLER_REGISTER;
    staticSamplers[1].RegisterSpace    = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 10;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.NumStaticSamplers         = 2;
    rootSigDesc.pStaticSamplers           = staticSamplers;
    rootSigDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT          hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr))
    {
        std::string errorMsg = std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        assert(false && "D3D12SerializeRootSignature failed");
    }

    CHECK_CALL(pRenderer->Device->CreateRootSignature(
        0,                         // nodeMask
        blob->GetBufferPointer(),  // pBloblWithRootSignature
        blob->GetBufferSize(),     // blobLengthInBytes
        IID_PPV_ARGS(ppRootSig))); // riid, ppvRootSignature
}

void CreateDescriptorHeaps(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppCBVSRVUAVHeap, ID3D12DescriptorHeap** ppSamplerHeap)
{
    //
    // CBVSRVUAV heap
    //
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors             = gNumIBLLUTs + gNumIBLEnvTextures + gNumIBLIrrTextures + 1024;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(
        &desc,
        IID_PPV_ARGS(ppCBVSRVUAVHeap)));

    //
    // Sampler heap
    //
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = 32;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(
        &desc,
        IID_PPV_ARGS(ppSamplerHeap)));
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
        if (bitmap.Empty())
        {
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
        if (bitmap.Empty())
        {
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

    // auto                               iblDir = GetAssetPath("IBL");
    // std::vector<std::filesystem::path> iblFiles;
    // for (auto& entry : std::filesystem::directory_iterator(iblDir))
    //{
    //     if (!entry.is_regular_file())
    //     {
    //         continue;
    //     }
    //     auto path = entry.path();
    //     auto ext  = path.extension();
    //     if (ext == ".ibl")
    //     {
    //         path = std::filesystem::relative(path, iblDir.parent_path());
    //         iblFiles.push_back(path);
    //     }
    // }

    std::vector<std::filesystem::path> iblFiles = {GetAssetPath("IBL/machine_shop_01_4k.ibl")};

    size_t maxEntries = std::min<size_t>(gNumIBLTextures, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i)
    {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl))
        {
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
            for (uint32_t i = 0; i < ibl.numLevels; ++i)
            {
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