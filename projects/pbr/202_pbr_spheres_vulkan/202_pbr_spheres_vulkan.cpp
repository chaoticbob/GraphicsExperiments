#include "window.h"

#include "vk_renderer.h"
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
    vec3  albedo;
    float roughness;
    float metalness;
    vec3  F0;
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
static bool     gEnableRayTracing = false;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static uint32_t gNumLights = 0;

void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout);
void CreateEnvironmentPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout);

/*
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
    */

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
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    if (!InitVulkan(renderer.get(), gEnableDebug, gEnableRayTracing)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    // PBR shaders
    std::vector<uint32_t> spirvVS;
    std::vector<uint32_t> spirvFS;
    {
        std::string shaderSource = LoadString("projects/201_pbr_spheres_d3d12/shaders.hlsl");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &spirvVS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &spirvFS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (FS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    VkShaderModule shaderModuleVS = VK_NULL_HANDLE;
    {
       VkShaderModuleCreateInfo createInfo   = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
       createInfo.codeSize                   = SizeInBytes(spirvVS);
       createInfo.pCode                      = DataPtr(spirvVS);

       CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &shaderModuleVS));
    }

    VkShaderModule shaderModuleFS = VK_NULL_HANDLE;
    {
       VkShaderModuleCreateInfo createInfo   = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
       createInfo.codeSize                   = SizeInBytes(spirvFS);
       createInfo.pCode                      = DataPtr(spirvFS);

       CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &shaderModuleFS));
    }

    // Draw texture shaders
    std::vector<uint32_t> drawTextureSpirvVS;
    std::vector<uint32_t> drawTextureSpirvFS;
    {
        std::string shaderSource = LoadString("projects/201_pbr_spheres_d3d12/drawtexture.hlsl");
        if (shaderSource.empty()) {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &drawTextureSpirvVS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &drawTextureSpirvFS, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (FS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    VkShaderModule drawTextureShaderModuleVS = VK_NULL_HANDLE;
    {
       VkShaderModuleCreateInfo createInfo   = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
       createInfo.codeSize                   = SizeInBytes(spirvVS);
       createInfo.pCode                      = DataPtr(spirvVS);

       CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &drawTextureShaderModuleVS));
    }

    VkShaderModule drawTextureShaderModuleFS = VK_NULL_HANDLE;
    {
       VkShaderModuleCreateInfo createInfo   = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
       createInfo.codeSize                   = SizeInBytes(spirvFS);
       createInfo.pCode                      = DataPtr(spirvFS);

       CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &drawTextureShaderModuleFS));
    }



    // *************************************************************************
    // PBR pipeline layout
    // *************************************************************************
    VulkanPipelineLayout pbrPipelineLayout = {};
    CreatePBRPipeline(renderer.get(), &pbrPipelineLayout);

    // *************************************************************************
    // Environment pipeline layout
    // *************************************************************************
    VulkanPipelineLayout envPipelineLayout = {};
    CreateEnvironmentPipeline(renderer.get(), &envPipelineLayout);

    // *************************************************************************
    // PBR pipeline state object
    // *************************************************************************
    VkPipeline pbrPipelineState;
    CHECK_CALL(CreateDrawNormalPipeline(
       renderer.get(),
       pbrPipelineLayout.PipelineLayout,
       shaderModuleVS,
       shaderModuleFS,
       GREX_DEFAULT_RTV_FORMAT,
       GREX_DEFAULT_DSV_FORMAT,
       &pbrPipelineState));

    // *************************************************************************
    // Environment pipeline state object
    // *************************************************************************
    VkPipeline envPipelineState;
    CHECK_CALL(CreateDrawTexturePipeline(
       renderer.get(),
       envPipelineLayout.PipelineLayout,
       drawTextureShaderModuleVS,
       drawTextureShaderModuleFS,
       GREX_DEFAULT_RTV_FORMAT,
       GREX_DEFAULT_DSV_FORMAT,
       &envPipelineState,
       VK_CULL_MODE_FRONT_BIT));

    /*
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
                materialParams.albedo             = vec3(0.8f, 0.8f, 0.9f);
                materialParams.roughness          = 0;
                materialParams.metalness          = 0;
                materialParams.F0                 = F0_Generic;

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
                    materialParams.metalness = 0;

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

                        materialParams.metalness += roughnessStep;
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
    */

    return 0;
}

void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout)
{
   // Descriptor set layout
   {
      std::vector<VkDescriptorSetLayoutBinding> bindings = {};
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 0;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 1;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 2;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 3;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 4;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 5;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkSamplerCreateInfo samplerInfo       = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
         samplerInfo.flags                     = 0;
         samplerInfo.magFilter                 = VK_FILTER_LINEAR;
         samplerInfo.minFilter                 = VK_FILTER_LINEAR;
         samplerInfo.mipmapMode                = VK_SAMPLER_MIPMAP_MODE_LINEAR;
         samplerInfo.addressModeU              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.addressModeV              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.addressModeW              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.mipLodBias                = 0;
         samplerInfo.anisotropyEnable          = VK_FALSE;
         samplerInfo.maxAnisotropy             = 0;
         samplerInfo.compareEnable             = VK_TRUE;
         samplerInfo.compareOp                 = VK_COMPARE_OP_LESS_OR_EQUAL;
         samplerInfo.minLod                    = 0;
         samplerInfo.maxLod                    = FLT_MAX;
         samplerInfo.borderColor               = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
         samplerInfo.unnormalizedCoordinates   = VK_FALSE;

         VkSampler clampedSampler = VK_NULL_HANDLE;
         CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &clampedSampler));

         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 6;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         binding.pImmutableSamplers           = &clampedSampler;

         bindings.push_back(binding);
      }
      {
         VkSamplerCreateInfo samplerInfo       = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
         samplerInfo.flags                     = 0;
         samplerInfo.magFilter                 = VK_FILTER_LINEAR;
         samplerInfo.minFilter                 = VK_FILTER_LINEAR;
         samplerInfo.mipmapMode                = VK_SAMPLER_MIPMAP_MODE_LINEAR;
         samplerInfo.addressModeU              = VK_SAMPLER_ADDRESS_MODE_REPEAT;
         samplerInfo.addressModeV              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.addressModeW              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.mipLodBias                = 0;
         samplerInfo.anisotropyEnable          = VK_FALSE;
         samplerInfo.maxAnisotropy             = 0;
         samplerInfo.compareEnable             = VK_TRUE;
         samplerInfo.compareOp                 = VK_COMPARE_OP_LESS_OR_EQUAL;
         samplerInfo.minLod                    = 0;
         samplerInfo.maxLod                    = FLT_MAX;
         samplerInfo.borderColor               = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
         samplerInfo.unnormalizedCoordinates   = VK_FALSE;

         VkSampler uWrapSampler = VK_NULL_HANDLE;
         CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &uWrapSampler));

         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 7;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         binding.pImmutableSamplers           = &uWrapSampler;

         bindings.push_back(binding);
      }

      VkDescriptorSetLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
      createInfo.bindingCount                    = CountU32(bindings);
      createInfo.pBindings                       = DataPtr(bindings);

      CHECK_CALL(vkCreateDescriptorSetLayout(
         pRenderer->Device,
         &createInfo,
         nullptr,
         &pLayout->DescriptorSetLayout));
   }

   VkPipelineLayoutCreateInfo createInfo  = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
   createInfo.setLayoutCount              = 1;
   createInfo.pSetLayouts                 = &pLayout->DescriptorSetLayout;

   CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, &pLayout->PipelineLayout));
}

void CreateEnvironmentPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout)
{
   // Descriptor set layout
   {
      std::vector<VkDescriptorSetLayoutBinding> bindings = {};
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 0;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 1;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 2;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         bindings.push_back(binding);
      }
      {
         VkSamplerCreateInfo samplerInfo       = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
         samplerInfo.flags                     = 0;
         samplerInfo.magFilter                 = VK_FILTER_LINEAR;
         samplerInfo.minFilter                 = VK_FILTER_LINEAR;
         samplerInfo.mipmapMode                = VK_SAMPLER_MIPMAP_MODE_LINEAR;
         samplerInfo.addressModeU              = VK_SAMPLER_ADDRESS_MODE_REPEAT;
         samplerInfo.addressModeV              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.addressModeW              = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         samplerInfo.mipLodBias                = 0;
         samplerInfo.anisotropyEnable          = VK_FALSE;
         samplerInfo.maxAnisotropy             = 0;
         samplerInfo.compareEnable             = VK_TRUE;
         samplerInfo.compareOp                 = VK_COMPARE_OP_LESS_OR_EQUAL;
         samplerInfo.minLod                    = 0;
         samplerInfo.maxLod                    = FLT_MAX;
         samplerInfo.borderColor               = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
         samplerInfo.unnormalizedCoordinates   = VK_FALSE;

         VkSampler uWrapSampler = VK_NULL_HANDLE;
         CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &uWrapSampler));

         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 3;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;
         binding.pImmutableSamplers           = &uWrapSampler;

         bindings.push_back(binding);
      }

      VkDescriptorSetLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
      createInfo.bindingCount                    = CountU32(bindings);
      createInfo.pBindings                       = DataPtr(bindings);

      CHECK_CALL(vkCreateDescriptorSetLayout(
         pRenderer->Device,
         &createInfo,
         nullptr,
         &pLayout->DescriptorSetLayout));
   }

   VkPipelineLayoutCreateInfo createInfo  = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
   createInfo.setLayoutCount              = 1;
   createInfo.pSetLayouts                 = &pLayout->DescriptorSetLayout;

   CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, &pLayout->PipelineLayout));
}

/*
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
*/

/*
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
*/

/*
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
*/

/*
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
*/