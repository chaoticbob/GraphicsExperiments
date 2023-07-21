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

#define ROW_METALLIC               0
#define ROW_ROUGHNESS_NON_METALLIC 1
#define ROW_ROUGHNESS_METALLIC     2
#define ROW_REFLECTANCE            3
#define ROW_CLEAR_COAT             4
#define ROW_CLEAR_COAT_ROUGHNESS   5
#define ROW_ANISOTROPY             6

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
    uint     multiscatter;
    uint     furnace;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    float metallic;
    float reflectance;
    float clearCoat;
    float clearCoatRoughness;
    float anisotropy;
};

struct PBRImplementationInfo
{
    std::string description;
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 3470;
static uint32_t gWindowHeight = 1920;
static bool     gEnableDebug  = true;

static uint32_t gGridStartX       = 485;
static uint32_t gGridStartY       = 15;
static uint32_t gGridTextHeight   = 28;
static uint32_t gCellStrideX      = 270;
static uint32_t gCellStrideY      = 270;
static uint32_t gCellResX         = gCellStrideX;
static uint32_t gCellResY         = gCellStrideY - gGridTextHeight;
static uint32_t gCellRenderResX   = gCellResX - 10;
static uint32_t gCellRenderResY   = gCellResY - 10;
static uint32_t gCellRenderStartX = gGridStartX + (gCellResX - gCellRenderResX) / 2;
static uint32_t gCellRenderStartY = gGridStartY + gGridTextHeight + (gCellResY - gCellRenderResY) / 2;

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
    ID3D12Resource** ppNormalBuffer,
    ID3D12Resource** ppTangentBuffer,
    ID3D12Resource** ppBitangetBuffer);
void CreateEnvironmentVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppTexCoordBuffer);
void CreateIBLTextures(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppBRDFLUT,
    ID3D12Resource** ppMultiscatterBRDFLUT,
    ID3D12Resource** ppIrradianceTexture,
    ID3D12Resource** ppEnvironmentTexture,
    uint32_t*        pEnvNumLevels,
    ID3D12Resource** ppFurnaceTexture);
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
        std::string shaderSource = LoadString("projects/252_pbr_material_properties/shaders.hlsl");

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
        std::string shaderSource = LoadString("projects/252_pbr_material_properties/drawtexture.hlsl");
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
        &pbrPipelineState,
        true /* enableTangents*/));

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
    ComPtr<ID3D12Resource> materialSphereTangentBuffer;
    ComPtr<ID3D12Resource> materialSphereBitangentBuffer;
    CreateMaterialSphereVertexBuffers(
        renderer.get(),
        &materialSphereNumIndices,
        &materialSphereIndexBuffer,
        &materialSpherePositionBuffer,
        &materialSphereNormalBuffer,
        &materialSphereTangentBuffer,
        &materialSphereBitangentBuffer);

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
    ComPtr<ID3D12Resource> multiscatterBRDFLUT;
    ComPtr<ID3D12Resource> irrTexture;
    ComPtr<ID3D12Resource> envTexture;
    ComPtr<ID3D12Resource> furnaceTexture;
    uint32_t               envNumLevels = 0;
    CreateIBLTextures(
        renderer.get(),
        &brdfLUT,
        &multiscatterBRDFLUT,
        &irrTexture,
        &envTexture,
        &envNumLevels,
        &furnaceTexture);

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

        // Multiscatter LUT
        CreateDescriptorTexture2D(renderer.get(), multiscatterBRDFLUT.Get(), descriptor);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Irradiance
        CreateDescriptorTexture2D(renderer.get(), irrTexture.Get(), descriptor);
        descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Environment
        CreateDescriptorTexture2D(renderer.get(), envTexture.Get(), descriptor, 0, envNumLevels);
    }

    // *************************************************************************
    // Material template
    // *************************************************************************
    ComPtr<ID3D12Resource> materialTemplateTexture;
    {
        auto bitmap = LoadImage8u(GetAssetPath("textures/material_properties_template.png"));
        CHECK_CALL(CreateTexture(
            renderer.get(),
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            DXGI_FORMAT_B8G8R8A8_UNORM,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            &materialTemplateTexture));
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "252_pbr_material_properties_d3d12");
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
            ImGui::Checkbox("Mutilscatter", reinterpret_cast<bool*>(&pSceneParams->multiscatter));
            ImGui::Checkbox("Furnace", reinterpret_cast<bool*>(&pSceneParams->furnace));
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
            descriptor.ptr += 2 * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            if (pSceneParams->furnace) {
                CreateDescriptorTexture2D(renderer.get(), furnaceTexture.Get(), descriptor);
                descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                CreateDescriptorTexture2D(renderer.get(), furnaceTexture.Get(), descriptor);

                pSceneParams->iblEnvironmentNumLevels = 1;
            }
            else {
                CreateDescriptorTexture2D(renderer.get(), irrTexture.Get(), descriptor);
                descriptor.ptr += renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                CreateDescriptorTexture2D(renderer.get(), envTexture.Get(), descriptor, 0, envNumLevels);

                pSceneParams->iblEnvironmentNumLevels = envNumLevels;
            }
        }

        // ---------------------------------------------------------------------

        UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

        ComPtr<ID3D12Resource> swapchainBuffer;
        CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        // Copy template to background
        {
            D3D12_RESOURCE_BARRIER copyBarriers[2] = {};
            //
            copyBarriers[0] = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, copyBarriers);

            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource                   = swapchainBuffer.Get();
            dst.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex            = 0;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource                   = materialTemplateTexture.Get();
            src.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex            = 0;
            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        // Descriptor heap
        ID3D12DescriptorHeap* heaps[1] = {descriptorHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);

        // Render stuff
        D3D12_RESOURCE_BARRIER preRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &preRenderBarrier);
        {
            // -----------------------------------------------------------------
            // Set render targets
            // -----------------------------------------------------------------
            commandList->OMSetRenderTargets(
                1,
                &renderer->SwapchainRTVDescriptorHandles[bufferIndex],
                false,
                &renderer->SwapchainDSVDescriptorHandles[bufferIndex]);

            // -----------------------------------------------------------------
            // Scene variables
            // -----------------------------------------------------------------
            // Camera matrices
            vec3 eyePosition = vec3(0, 0, 0.85f);
            mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gCellRenderResX / static_cast<float>(gCellRenderResY), 0.1f, 10000.0f);
            mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));

            // Set constant buffer values
            pSceneParams->viewProjectionMatrix    = projMat * viewMat;
            pSceneParams->eyePosition             = eyePosition;
            pSceneParams->numLights               = 1;
            pSceneParams->lights[0].position      = vec3(-5, 5, 3);
            pSceneParams->lights[0].color         = vec3(1, 1, 1);
            pSceneParams->lights[0].intensity     = 1.5f;
            pSceneParams->iblEnvironmentNumLevels = envNumLevels;

            // -----------------------------------------------------------------
            // Descriptors
            // -----------------------------------------------------------------
            commandList->SetGraphicsRootSignature(pbrRootSig.Get());
            // SceneParams (b0)
            commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
            // IBL textures (t3, t4, t5)
            commandList->SetGraphicsRootDescriptorTable(3, descriptorHeap->GetGPUDescriptorHandleForHeapStart());

            // -----------------------------------------------------------------
            // Pipeline state
            // -----------------------------------------------------------------
            commandList->SetPipelineState(pbrPipelineState.Get());

            // -----------------------------------------------------------------
            // Index and vertex buffers
            // -----------------------------------------------------------------
            // Index buffer
            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation          = materialSphereIndexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes             = static_cast<UINT>(materialSphereIndexBuffer->GetDesc().Width);
            ibv.Format                  = DXGI_FORMAT_R32_UINT;
            commandList->IASetIndexBuffer(&ibv);

            // Vertex buffers
            D3D12_VERTEX_BUFFER_VIEW vbvs[4] = {};
            // Position
            vbvs[0].BufferLocation = materialSpherePositionBuffer->GetGPUVirtualAddress();
            vbvs[0].SizeInBytes    = static_cast<UINT>(materialSpherePositionBuffer->GetDesc().Width);
            vbvs[0].StrideInBytes  = 12;
            // Normal
            vbvs[1].BufferLocation = materialSphereNormalBuffer->GetGPUVirtualAddress();
            vbvs[1].SizeInBytes    = static_cast<UINT>(materialSphereNormalBuffer->GetDesc().Width);
            vbvs[1].StrideInBytes  = 12;
            // Tangent
            vbvs[2].BufferLocation = materialSphereTangentBuffer->GetGPUVirtualAddress();
            vbvs[2].SizeInBytes    = static_cast<UINT>(materialSphereTangentBuffer->GetDesc().Width);
            vbvs[2].StrideInBytes  = 12;
            // Bitangent
            vbvs[3].BufferLocation = materialSphereBitangentBuffer->GetGPUVirtualAddress();
            vbvs[3].SizeInBytes    = static_cast<UINT>(materialSphereBitangentBuffer->GetDesc().Width);
            vbvs[3].StrideInBytes  = 12;

            commandList->IASetVertexBuffers(0, 4, vbvs);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // -----------------------------------------------------------------
            // Draw material spheres
            // -----------------------------------------------------------------
            const float clearColor[4] = {1, 1, 1, 1};
            uint32_t    cellY         = gCellRenderStartY;
            float       dt            = 1.0f / 10;
            for (uint32_t yi = 0; yi < 7; ++yi) {
                uint32_t cellX = gCellRenderStartX;
                float    t     = 0;
                for (uint32_t xi = 0; xi < 11; ++xi) {
                    D3D12_RECT cellRect = {};
                    cellRect.left       = cellX;
                    cellRect.top        = cellY;
                    cellRect.right      = (cellX + gCellRenderResX);
                    cellRect.bottom     = (cellY + gCellRenderResY);

                    if (pSceneParams->furnace) {
                        commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 1, &cellRect);
                    }
                    commandList->ClearDepthStencilView(renderer->SwapchainDSVDescriptorHandles[bufferIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 1, &cellRect);

                    // ---------------------------------------------------------
                    // Set viewport and scissor
                    // ---------------------------------------------------------
                    D3D12_VIEWPORT viewport = {
                        static_cast<float>(cellX),
                        static_cast<float>(cellY),
                        static_cast<float>(gCellRenderResX),
                        static_cast<float>(gCellRenderResY),
                        0,
                        1};
                    commandList->RSSetViewports(1, &viewport);
                    commandList->RSSetScissorRects(1, &cellRect);

                    // ---------------------------------------------------------
                    // Draw material sphere
                    // ---------------------------------------------------------
                    MaterialParameters materialParams = {};
                    materialParams.baseColor          = vec3(1.0f, 1.0f, 1.0f);
                    materialParams.roughness          = 0;
                    materialParams.metallic           = 0;
                    materialParams.reflectance        = 0.5;
                    materialParams.clearCoat          = 0;
                    materialParams.clearCoatRoughness = 0;

                    switch (yi) {
                        default: break;
                        case ROW_METALLIC: {
                            materialParams.baseColor = F0_MetalChromium;
                            materialParams.metallic  = t;
                            materialParams.roughness = 0;
                        } break;

                        case ROW_ROUGHNESS_NON_METALLIC: {
                            materialParams.baseColor = vec3(0, 0, 0.75f);
                            materialParams.roughness = std::max(0.045f, t);
                        } break;

                        case ROW_ROUGHNESS_METALLIC: {
                            materialParams.baseColor = pSceneParams->furnace ? vec3(1) : F0_MetalGold;
                            materialParams.roughness = std::max(0.045f, t);
                            materialParams.metallic  = 1.0;
                        } break;

                        case ROW_REFLECTANCE: {
                            materialParams.baseColor   = vec3(0.75f, 0, 0);
                            materialParams.roughness   = 0.2f;
                            materialParams.metallic    = 0;
                            materialParams.reflectance = t;
                        } break;

                        case ROW_CLEAR_COAT: {
                            materialParams.baseColor = vec3(0.75f, 0, 0);
                            materialParams.roughness = 0.8f;
                            materialParams.metallic  = 1.0f;
                            materialParams.clearCoat = t;
                        } break;

                        case ROW_CLEAR_COAT_ROUGHNESS: {
                            materialParams.baseColor          = vec3(0.75f, 0, 0);
                            materialParams.roughness          = 0.8f;
                            materialParams.metallic           = 1.0f;
                            materialParams.clearCoat          = 1;
                            materialParams.clearCoatRoughness = std::max(0.045f, t);
                        } break;

                        case ROW_ANISOTROPY: {
                            materialParams.baseColor  = F0_MetalZinc;
                            materialParams.roughness  = 0.45f;
                            materialParams.metallic   = 1.0f;
                            materialParams.anisotropy = t;
                        } break;
                    }

                    glm::mat4 modelMat = glm::mat4(1);
                    // DrawParams (b1)
                    commandList->SetGraphicsRoot32BitConstants(1, 16, &modelMat, 0);
                    // MaterialParams (b2)
                    commandList->SetGraphicsRoot32BitConstants(2, 9, &materialParams, 0);

                    // Draw
                    commandList->DrawIndexedInstanced(materialSphereNumIndices, 1, 0, 0, 0);

                    // ---------------------------------------------------------
                    // Next cell
                    // ---------------------------------------------------------
                    cellX += gCellStrideX;
                    t += dt;
                }
                cellY += gCellStrideY;
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
    // IBL textures (t3, t4, t5, t6)
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 4;
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
    rootParameters[2].Constants.Num32BitValues = 9;
    rootParameters[2].Constants.ShaderRegister = 2;
    rootParameters[2].Constants.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    // IBL textures (t3, t4, t5, t6)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &range;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
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
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].MinLOD           = 0;
    staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister   = 33;
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

void CreateMaterialSphereVertexBuffers(
    DxRenderer*      pRenderer,
    uint32_t*        pNumIndices,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppPositionBuffer,
    ID3D12Resource** ppNormalBuffer,
    ID3D12Resource** ppTangentBuffer,
    ID3D12Resource** ppBitangetBuffer)
{
    TriMesh mesh = TriMesh::Sphere(0.42f, 256, 256, {.enableNormals = true, .enableTangents = true});

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

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTangents()),
        DataPtr(mesh.GetTangents()),
        ppTangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetBitangents()),
        DataPtr(mesh.GetBitangents()),
        ppBitangetBuffer));
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
    ID3D12Resource** ppMultiscatterBRDFLUT,
    ID3D12Resource** ppIrradianceTexture,
    ID3D12Resource** ppEnvironmentTexture,
    uint32_t*        pEnvNumLevels,
    ID3D12Resource** ppFurnaceTexture)
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

    // Furnace
    {
        BitmapRGBA32f bitmap(32, 16);
        bitmap.Fill(PixelRGBA32f{1, 1, 1, 1});

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            ppFurnaceTexture));
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