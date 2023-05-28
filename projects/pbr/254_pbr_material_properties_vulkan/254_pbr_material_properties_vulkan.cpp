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

struct PBRSceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t numLights;
    Light    lights[8];
    uint     iblEnvironmentNumLevels;
    uint     multiscatter;
    uint     furnace;
};

struct EnvSceneParameters
{
    mat4 MVP;
};

struct DrawParameters
{
    mat4 ModelMatrix;
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
static uint32_t gWindowWidth      = 3470;
static uint32_t gWindowHeight     = 1920;
static bool     gEnableDebug      = true;
static bool     gEnableRayTracing = false;

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

void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout);
void CreateEnvironmentPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout);
void CreateMaterialSphereVertexBuffers(
    VulkanRenderer* pRenderer,
    uint32_t*       pNumIndices,
    VulkanBuffer*   pIndexBuffer,
    VulkanBuffer*   pPositionBuffer,
    VulkanBuffer*   pNormalBuffer,
    VulkanBuffer*   pTangentBuffer,
    VulkanBuffer*   pBitangetBuffer);
void CreateEnvironmentVertexBuffers(
    VulkanRenderer* pRenderer,
    uint32_t*       pNumIndices,
    VulkanBuffer*   pIndexBuffer,
    VulkanBuffer*   pPositionBuffer,
    VulkanBuffer*   pTexCoordBuffer);
void CreateIBLTextures(
    VulkanRenderer* pRenderer,
    VulkanImage*    pBRDFLUT,
    VulkanImage*    pMultiscatterBRDFLUT,
    VulkanImage*    pIrradianceTexture,
    VulkanImage*    pEnvironmentTexture,
    uint32_t*       EnvNumLevels,
    VulkanImage*    ppFurnaceTexture);
void CreateDescriptorBuffer(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout pDescriptorSetLayout,
    VulkanBuffer*         pBuffer);
void WritePBRDescriptors(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pDescriptorBuffer,
    const VulkanBuffer*   pSceneParamsBuffer,
    const VulkanImage*    pBRDFLUT,
    const VulkanImage*    pMultiscatterBRDFLUT,
    const VulkanImage*    pIrradianceTexture,
    const VulkanImage*    pEnvTexture);
void WriteEnvDescriptors(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pDescrptorBuffer,
    VulkanImage*          pEnvTexture);

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
        std::string shaderSource = LoadString("projects/253_pbr_material_properties_d3d12/shaders.hlsl");

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
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    VkShaderModule shaderModuleVS = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvVS);
        createInfo.pCode                    = DataPtr(spirvVS);

        CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &shaderModuleVS));
    }

    VkShaderModule shaderModuleFS = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvFS);
        createInfo.pCode                    = DataPtr(spirvFS);

        CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &shaderModuleFS));
    }

    // Draw texture shaders
    std::vector<uint32_t> drawTextureSpirvVS;
    std::vector<uint32_t> drawTextureSpirvFS;
    {
        std::string shaderSource = LoadString("projects/253_pbr_material_properties_d3d12/drawtexture.hlsl");
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
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    VkShaderModule drawTextureShaderModuleVS = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(drawTextureSpirvVS);
        createInfo.pCode                    = DataPtr(drawTextureSpirvVS);

        CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &drawTextureShaderModuleVS));
    }

    VkShaderModule drawTextureShaderModuleFS = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(drawTextureSpirvFS);
        createInfo.pCode                    = DataPtr(drawTextureSpirvFS);

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
    VkPipeline pbrPipelineState = VK_NULL_HANDLE;
    CHECK_CALL(CreateDrawNormalPipeline(
        renderer.get(),
        pbrPipelineLayout.PipelineLayout,
        shaderModuleVS,
        shaderModuleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pbrPipelineState,
        true, // enableTangents
        VK_CULL_MODE_BACK_BIT,
        "vsmain",
        "psmain"));

    // *************************************************************************
    // Environment pipeline state object
    // *************************************************************************
    VkPipeline envPipelineState = VK_NULL_HANDLE;
    CHECK_CALL(CreateDrawTexturePipeline(
        renderer.get(),
        envPipelineLayout.PipelineLayout,
        drawTextureShaderModuleVS,
        drawTextureShaderModuleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &envPipelineState,
        VK_CULL_MODE_FRONT_BIT));

    // *************************************************************************
    // Scene Params Buffer
    // *************************************************************************
    VulkanBuffer pbrSceneParamsBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        Align<size_t>(sizeof(PBRSceneParameters), 256),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        0,
        &pbrSceneParamsBuffer));

    // *************************************************************************
    // Material sphere vertex buffers
    // *************************************************************************
    uint32_t     materialSphereNumIndices = 0;
    VulkanBuffer materialSphereIndexBuffer;
    VulkanBuffer materialSpherePositionBuffer;
    VulkanBuffer materialSphereNormalBuffer;
    VulkanBuffer materialSphereTangentBuffer;
    VulkanBuffer materialSphereBitangentBuffer;
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
    uint32_t     envNumIndices = 0;
    VulkanBuffer envIndexBuffer;
    VulkanBuffer envPositionBuffer;
    VulkanBuffer envTexCoordBuffer;
    CreateEnvironmentVertexBuffers(
        renderer.get(),
        &envNumIndices,
        &envIndexBuffer,
        &envPositionBuffer,
        &envTexCoordBuffer);

    // *************************************************************************
    // IBL texture
    // *************************************************************************
    VulkanImage brdfLUT;
    VulkanImage multiscatterBRDFLUT;
    VulkanImage irrTexture;
    VulkanImage envTexture;
    VulkanImage furnaceTexture;
    uint32_t    envNumLevels = 0;
    CreateIBLTextures(
        renderer.get(),
        &brdfLUT,
        &multiscatterBRDFLUT,
        &irrTexture,
        &envTexture,
        &envNumLevels,
        &furnaceTexture);

    // *************************************************************************
    // Descriptor buffers
    // *************************************************************************
    VulkanBuffer pbrDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), pbrPipelineLayout.DescriptorSetLayout, &pbrDescriptorBuffer);

    WritePBRDescriptors(
        renderer.get(),
        pbrPipelineLayout.DescriptorSetLayout,
        &pbrDescriptorBuffer,
        &pbrSceneParamsBuffer,
        &multiscatterBRDFLUT,
        &brdfLUT,
        &irrTexture,
        &envTexture);

    VulkanBuffer envDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), envPipelineLayout.DescriptorSetLayout, &envDescriptorBuffer);

    WriteEnvDescriptors(
        renderer.get(),
        envPipelineLayout.DescriptorSetLayout,
        &envDescriptorBuffer,
        &envTexture);

    // *************************************************************************
    // Material template
    // *************************************************************************
    VulkanImage materialTemplateTexture = {};
    {
        auto bitmap = LoadImage8u(GetAssetPath("textures/material_properties_template.png"));
        CHECK_CALL(CreateTexture(
            renderer.get(),
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            VK_FORMAT_B8G8R8A8_UNORM,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            &materialTemplateTexture));
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "254_pbr_material_properties_vulkan");
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight())) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Render pass to draw ImGui
    // *************************************************************************
    std::vector<VulkanAttachmentInfo> colorAttachmentInfos = {
        {GREX_DEFAULT_RTV_FORMAT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, renderer->SwapchainImageUsage}
    };

    VulkanRenderPass renderPass = {};
    CHECK_CALL(CreateRenderPass(renderer.get(), colorAttachmentInfos, {}, gWindowWidth, gWindowHeight, &renderPass));


    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForVulkan(renderer.get(), renderPass.RenderPass)) {
        assert(false && "Window::InitImGuiForVulkan failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain image views, depth buffers/views
    // *************************************************************************
    std::vector<VkImage>     images;
    std::vector<VulkanImage> depthImages;
    std::vector<VkImageView> imageViews;
    std::vector<VkImageView> depthViews;
    {
       CHECK_CALL(GetSwapchainImages(renderer.get(), images));

       for (auto& image : images) {
          // Create swap chain images
          VkImageViewCreateInfo createInfo           = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
          createInfo.image                           = image;
          createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
          createInfo.format                          = GREX_DEFAULT_RTV_FORMAT;
          createInfo.components                      = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
          createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
          createInfo.subresourceRange.baseMipLevel   = 0;
          createInfo.subresourceRange.levelCount     = 1;
          createInfo.subresourceRange.baseArrayLayer = 0;
          createInfo.subresourceRange.layerCount     = 1;

          VkImageView imageView = VK_NULL_HANDLE;
          CHECK_CALL(vkCreateImageView(renderer->Device, &createInfo, nullptr, &imageView));

          imageViews.push_back(imageView);
       }

       size_t imageCount = images.size();

       depthImages.resize(images.size());

       for (int depthIndex = 0; depthIndex < imageCount; depthIndex++) {
          // Create depth images
          CHECK_CALL(CreateDSV(renderer.get(), window->GetWidth(), window->GetHeight(), &depthImages[depthIndex]));

          VkImageViewCreateInfo createInfo           = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
          createInfo.image                           = depthImages[depthIndex].Image;
          createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
          createInfo.format                          = GREX_DEFAULT_DSV_FORMAT;
          createInfo.components                      = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
          createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
          createInfo.subresourceRange.baseMipLevel   = 0;
          createInfo.subresourceRange.levelCount     = 1;
          createInfo.subresourceRange.baseArrayLayer = 0;
          createInfo.subresourceRange.layerCount     = 1;

          VkImageView depthView = VK_NULL_HANDLE;
          CHECK_CALL(vkCreateImageView(renderer->Device, &createInfo, nullptr, &depthView));

          depthViews.push_back(depthView);
       }
    }

    // *************************************************************************
    // Command buffer
    // *************************************************************************
    CommandObjects cmdBuf = {};
    {
       CHECK_CALL(CreateCommandBuffer(renderer.get(), 0, &cmdBuf));
    }

    // *************************************************************************
    // Persistent map scene parameters
    // *************************************************************************
    PBRSceneParameters* pPBRSceneParams = nullptr;
    vmaMapMemory(renderer->Allocator, pbrSceneParamsBuffer.Allocation, reinterpret_cast<void**>(&pPBRSceneParams));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color = { {0.0f, 0.0f, 0.2f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    while (window->PollEvents()) {
        window->ImGuiNewFrameVulkan();

        if (ImGui::Begin("Scene")) {
            ImGui::Checkbox("Mutilscatter", reinterpret_cast<bool*>(&pPBRSceneParams->multiscatter));
            ImGui::Checkbox("Furnace", reinterpret_cast<bool*>(&pPBRSceneParams->furnace));
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        UINT bufferIndex = 0;
        if (AcquireNextImage(renderer.get(), &bufferIndex)) {
            assert(false && "AcquireNextImage failed");
            break;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        // Render stuff
        {
            // Copy template to background
            {
                CmdTransitionImageLayout(
                    cmdBuf.CommandBuffer,
                    materialTemplateTexture.Image,
                    0,
                    1,
                    0,
                    1,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    RESOURCE_STATE_COMPUTE_SHADER_RESOURCE,
                    RESOURCE_STATE_TRANSFER_SRC);

                CmdTransitionImageLayout(
                    cmdBuf.CommandBuffer,
                    images[bufferIndex],
                    0,
                    1,
                    0,
                    1,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    RESOURCE_STATE_PRESENT,
                    RESOURCE_STATE_TRANSFER_DST);

                VkImageCopy regions                   = {};
                regions.extent.width                  = gWindowWidth;
                regions.extent.height                 = gWindowHeight;
                regions.extent.depth                  = 1;
                regions.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                regions.srcSubresource.baseArrayLayer = 0;
                regions.srcSubresource.layerCount     = 1;
                regions.srcSubresource.mipLevel       = 0;
                regions.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                regions.dstSubresource.baseArrayLayer = 0;
                regions.dstSubresource.layerCount     = 1;
                regions.dstSubresource.mipLevel       = 0;

                vkCmdCopyImage(
                    cmdBuf.CommandBuffer,
                    materialTemplateTexture.Image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    images[bufferIndex],
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &regions);

                CmdTransitionImageLayout(
                    cmdBuf.CommandBuffer,
                    images[bufferIndex],
                    0,
                    1,
                    0,
                    1,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    RESOURCE_STATE_TRANSFER_DST,
                    RESOURCE_STATE_RENDER_TARGET);

                CmdTransitionImageLayout(
                    cmdBuf.CommandBuffer,
                    materialTemplateTexture.Image,
                    0,
                    1,
                    0,
                    1,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    RESOURCE_STATE_TRANSFER_SRC,
                    RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
            }

            VkRenderingAttachmentInfo colorAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView                 = imageViews[bufferIndex];
            colorAttachment.imageLayout               = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue                = clearValues[0];

            VkRenderingAttachmentInfo depthAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView                 = depthViews[bufferIndex];
            depthAttachment.imageLayout               = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue                = clearValues[1];

            VkRenderingInfo vkri                      = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            vkri.layerCount                           = 1;
            vkri.colorAttachmentCount                 = 1;
            vkri.pColorAttachments                    = &colorAttachment;
            vkri.pDepthAttachment                     = &depthAttachment;
            vkri.renderArea.extent.width              = gWindowWidth;
            vkri.renderArea.extent.height             = gWindowHeight;

            vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);

            VkViewport viewport = {0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {0, 0, gWindowWidth, gWindowHeight};
            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

            // -----------------------------------------------------------------
            // Scene variables
            // -----------------------------------------------------------------
            // Camera matrices
            vec3 eyePosition = vec3(0, 0, 0.85f);
            mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gCellRenderResX / static_cast<float>(gCellRenderResY), 0.1f, 10000.0f);
            mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));

            // Set constant buffer values
            pPBRSceneParams->viewProjectionMatrix    = projMat * viewMat;
            pPBRSceneParams->eyePosition             = eyePosition;
            pPBRSceneParams->numLights               = 1;
            pPBRSceneParams->lights[0].position      = vec3(-5, 5, 3);
            pPBRSceneParams->lights[0].color         = vec3(1, 1, 1);
            pPBRSceneParams->lights[0].intensity     = 1.5f;
            pPBRSceneParams->iblEnvironmentNumLevels = envNumLevels;

            // -----------------------------------------------------------------
            // Pipeline state
            // -----------------------------------------------------------------
            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipelineState);

            // -----------------------------------------------------------------
            // Index and vertex buffers
            // -----------------------------------------------------------------
            // Index buffer
            vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, materialSphereIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

            // Vertex buffers
            VkBuffer vertexBuffers[] =
                {
                    materialSpherePositionBuffer.Buffer,
                    materialSphereNormalBuffer.Buffer,
                    materialSphereTangentBuffer.Buffer,
                    materialSphereBitangentBuffer.Buffer
                };

            VkDeviceSize offsets[]       = {0, 0, 0, 0};
            vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 4, vertexBuffers, offsets);

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
                    VkRect2D cellRect      = {};
                    cellRect.offset.x      = cellX;
                    cellRect.offset.y      = cellY;
                    cellRect.extent.width  = gCellRenderResX;
                    cellRect.extent.height = gCellRenderResY;

                    if (pPBRSceneParams->furnace) {
                        vkCmdEndRendering(cmdBuf.CommandBuffer);

                        CmdTransitionImageLayout(
                            cmdBuf.CommandBuffer,
                            images[bufferIndex],
                            0,
                            1,
                            0,
                            1,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            RESOURCE_STATE_RENDER_TARGET,
                            RESOURCE_STATE_TRANSFER_DST);

                        VkImageSubresourceRange range = {};
                        range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
                        range.baseArrayLayer          = 0;
                        range.baseMipLevel            = 0;
                        range.layerCount              = 1;
                        range.levelCount              = 1;

                        vkCmdClearColorImage(
                            cmdBuf.CommandBuffer,
                            images[bufferIndex],
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            &clearValues[0].color,
                            1,
                            &range);

                        CmdTransitionImageLayout(
                            cmdBuf.CommandBuffer,
                            images[bufferIndex],
                            0,
                            1,
                            0,
                            1,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            RESOURCE_STATE_TRANSFER_DST,
                            RESOURCE_STATE_RENDER_TARGET);

                        vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);
                    }
                    {
                        vkCmdEndRendering(cmdBuf.CommandBuffer);

                        CmdTransitionImageLayout(
                            cmdBuf.CommandBuffer,
                            depthImages[bufferIndex].Image,
                            0,
                            1,
                            0,
                            1,
                            VK_IMAGE_ASPECT_DEPTH_BIT,
                            RESOURCE_STATE_DEPTH_STENCIL,
                            RESOURCE_STATE_TRANSFER_DST);

                        VkImageSubresourceRange range = {};
                        range.aspectMask              = VK_IMAGE_ASPECT_DEPTH_BIT;
                        range.baseArrayLayer          = 0;
                        range.baseMipLevel            = 0;
                        range.layerCount              = 1;
                        range.levelCount              = 1;

                        vkCmdClearDepthStencilImage(
                            cmdBuf.CommandBuffer,
                            depthImages[bufferIndex].Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            &clearValues[1].depthStencil,
                            1,
                            &range);

                        CmdTransitionImageLayout(
                            cmdBuf.CommandBuffer,
                            depthImages[bufferIndex].Image,
                            0,
                            1,
                            0,
                            1,
                            VK_IMAGE_ASPECT_DEPTH_BIT,
                            RESOURCE_STATE_TRANSFER_DST,
                            RESOURCE_STATE_DEPTH_STENCIL);

                        vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);
                    }

                    // ---------------------------------------------------------
                    // Set viewport and scissor
                    // ---------------------------------------------------------
                    VkViewport viewport = {
                        static_cast<float>(cellX),
                        static_cast<float>(cellY),
                        static_cast<float>(gCellRenderResX),
                        static_cast<float>(gCellRenderResY),
                        0,
                        1};
                    vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);
                    vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &cellRect);

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
                            materialParams.baseColor = pPBRSceneParams->furnace ? vec3(1) : F0_MetalGold;
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
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    // MaterialParams (b2)
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(MaterialParameters), &materialParams);

                    // Draw
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, materialSphereNumIndices, 1, 0, 0, 0);

                    // ---------------------------------------------------------
                    // Next cell
                    // ---------------------------------------------------------
                    cellX += gCellStrideX;
                    t += dt;
                }
                cellY += gCellStrideY;
            }

            vkCmdEndRendering(cmdBuf.CommandBuffer);

            // Setup render passes and draw ImGui
            {
                VkRenderPassAttachmentBeginInfo attachmentBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
                attachmentBeginInfo.pNext                           = 0;
                attachmentBeginInfo.attachmentCount                 = 1;
                attachmentBeginInfo.pAttachments                    = &imageViews[bufferIndex];

                VkRenderPassBeginInfo beginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                beginInfo.pNext                 = &attachmentBeginInfo;
                beginInfo.renderPass            = renderPass.RenderPass;
                beginInfo.framebuffer           = renderPass.Framebuffer;
                beginInfo.renderArea            = scissor;

                vkCmdBeginRenderPass(cmdBuf.CommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Draw ImGui
                window->ImGuiRenderDrawData(renderer.get(), cmdBuf.CommandBuffer);

                vkCmdEndRenderPass(cmdBuf.CommandBuffer);
            }

            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                images[bufferIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_RENDER_TARGET,
                RESOURCE_STATE_PRESENT);
        }

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        // Execute command buffer
        CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

        // Wait for the GPU to finish the work
        if (!WaitForGpu(renderer.get())) {
            assert(false && "WaitForGpu failed");
            break;
        }

        // Present
        if (!SwapchainPresent(renderer.get(), bufferIndex)) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout)
{
    // Descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {};
        // ConstantBuffer<SceneParameters>      SceneParams        : register(b0);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 0;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                          IBLIntegrationLUT             : register(t3);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 3;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                          IBLIntegrationMultiscatterLUT : register(t4);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 4;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                          IBLIrradianceMap              : register(t5);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 5;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                          IBLEnvironmentMap             : register(t6);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 6;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                       IBLIntegrationSampler         : register(s32);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 32;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                       IBLMapSampler                 : register(s33);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 33;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        createInfo.bindingCount                    = CountU32(bindings);
        createInfo.pBindings                       = DataPtr(bindings);

        CHECK_CALL(vkCreateDescriptorSetLayout(
            pRenderer->Device,
            &createInfo,
            nullptr,
            &pLayout->DescriptorSetLayout));
    }

    std::vector<VkPushConstantRange> push_constants;
    {
        VkPushConstantRange push_constant = {};
        push_constant.offset              = 0;
        push_constant.size                = sizeof(DrawParameters) + sizeof(MaterialParameters);
        push_constant.stageFlags          = VK_SHADER_STAGE_ALL_GRAPHICS;
        push_constants.push_back(push_constant);
    }

    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.setLayoutCount             = 1;
    createInfo.pSetLayouts                = &pLayout->DescriptorSetLayout;
    createInfo.pushConstantRangeCount     = CountU32(push_constants);
    createInfo.pPushConstantRanges        = DataPtr(push_constants);

    CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, &pLayout->PipelineLayout));
}

void CreateEnvironmentPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout)
{
    // Descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {};

        // [[vk::push_constant]]
        // ConstantBuffer<SceneParameters> SceneParams       : register(b0);

        // SamplerState                    IBLMapSampler     : register(s1);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 1;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                       IBLEnvironmentMap : register(t2);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 2;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        createInfo.bindingCount                    = CountU32(bindings);
        createInfo.pBindings                       = DataPtr(bindings);

        CHECK_CALL(vkCreateDescriptorSetLayout(
            pRenderer->Device,
            &createInfo,
            nullptr,
            &pLayout->DescriptorSetLayout));
    }

    VkPushConstantRange push_constant = {};
    push_constant.offset              = 0;
    push_constant.size                = sizeof(EnvSceneParameters);
    push_constant.stageFlags          = VK_SHADER_STAGE_ALL_GRAPHICS;

    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.setLayoutCount             = 1;
    createInfo.pSetLayouts                = &pLayout->DescriptorSetLayout;
    createInfo.pushConstantRangeCount     = 1;
    createInfo.pPushConstantRanges        = &push_constant;

    CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, &pLayout->PipelineLayout));
}

void CreateMaterialSphereVertexBuffers(
    VulkanRenderer* pRenderer,
    uint32_t*       pNumIndices,
    VulkanBuffer*   pIndexBuffer,
    VulkanBuffer*   pPositionBuffer,
    VulkanBuffer*   pNormalBuffer,
    VulkanBuffer*   pTangentBuffer,
    VulkanBuffer*   pBitangetBuffer)
{
    TriMesh mesh = TriMesh::Sphere(0.42f, 256, 256, {.enableNormals = true, .enableTangents = true});

    *pNumIndices = 3 * mesh.GetNumTriangles();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pNormalBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTangents()),
        DataPtr(mesh.GetTangents()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pTangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetBitangents()),
        DataPtr(mesh.GetBitangents()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pBitangetBuffer));
}

void CreateEnvironmentVertexBuffers(
    VulkanRenderer* pRenderer,
    uint32_t*       pNumIndices,
    VulkanBuffer*   pIndexBuffer,
    VulkanBuffer*   pPositionBuffer,
    VulkanBuffer*   pTexCoordBuffer)
{
    TriMesh mesh = TriMesh::Sphere(100, 64, 64, {.enableTexCoords = true, .faceInside = true});

    *pNumIndices = 3 * mesh.GetNumTriangles();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        pTexCoordBuffer));
}

void CreateIBLTextures(
    VulkanRenderer* pRenderer,
    VulkanImage*    pBRDFLUT,
    VulkanImage*    pMultiscatterBRDFLUT,
    VulkanImage*    pIrradianceTexture,
    VulkanImage*    pEnvironmentTexture,
    uint32_t*       pEnvNumLevels,
    VulkanImage*    pFurnaceTexture)
{
    // BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
        if (bitmap.Empty()) {
            assert(false && "Load image failed");
            return;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            VK_FORMAT_R32G32B32A32_SFLOAT,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pBRDFLUT));
    }

    // Multiscatter BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut_ms.hdr"));
        if (bitmap.Empty()) {
            assert(false && "Load image failed");
            return;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            VK_FORMAT_R32G32B32A32_SFLOAT,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pMultiscatterBRDFLUT));
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
            VK_FORMAT_R32G32B32A32_SFLOAT,
            ibl.irradianceMap.GetSizeInBytes(),
            ibl.irradianceMap.GetPixels(),
            pIrradianceTexture));
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
            VK_FORMAT_R32G32B32A32_SFLOAT,
            mipOffsets,
            ibl.environmentMap.GetSizeInBytes(),
            ibl.environmentMap.GetPixels(),
            pEnvironmentTexture));
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
            VK_FORMAT_R32G32B32A32_SFLOAT,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pFurnaceTexture));
    }
}

void CreateDescriptorBuffer(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pBuffer)
{
    VkDeviceSize size = 0;
    fn_vkGetDescriptorSetLayoutSizeEXT(pRenderer->Device, descriptorSetLayout, &size);

    VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    CHECK_CALL(CreateBuffer(
        pRenderer,  // pRenderer
        size,       // srcSize
        nullptr,    // pSrcData
        usageFlags, // usageFlags
        0,          // minAlignment
        pBuffer));  // pBuffer
}

void WritePBRDescriptors(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pDescriptorBuffer,
    const VulkanBuffer*   pSceneParamsBuffer,
    const VulkanImage*    pBRDFLUT,
    const VulkanImage*    pMultiscatterBRDFLUT,
    const VulkanImage*    pIrradianceTexture,
    const VulkanImage*    pEnvTexture)
{
    char* pDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        pRenderer->Allocator,
        pDescriptorBuffer->Allocation,
        reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // ConstantBuffer<SceneParameters>    SceneParams                   : register(b0);
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        0, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        pSceneParamsBuffer);

    // Set via push constants
    // ConstantBuffer<DrawParameters>     DrawParams                    : register(b1);
    // ConstantBuffer<MaterialParameters> MaterialParams                : register(b2);

    // Texture2D                          IBLIntegrationLUT             : register(t3);
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            pBRDFLUT,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            GREX_ALL_SUBRESOURCES,
            &imageView));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            3, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    // Texture2D                          IBLIntegrationMultiscatterLUT : register(t4);
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            pMultiscatterBRDFLUT,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            GREX_ALL_SUBRESOURCES,
            &imageView));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            4, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    // Texture2D                          IBLIrradianceMap              : register(t5);
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            pIrradianceTexture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            GREX_ALL_SUBRESOURCES,
            &imageView));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            5, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    // Texture2D                          IBLEnvironmentMap             : register(t6);
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            pEnvTexture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            GREX_ALL_SUBRESOURCES,
            &imageView));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            6, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    // SamplerState                       IBLIntegrationSampler         : register(s32);
    {
        VkSamplerCreateInfo samplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.flags                   = 0;
        samplerInfo.magFilter               = VK_FILTER_LINEAR;
        samplerInfo.minFilter               = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.mipLodBias              = 0;
        samplerInfo.anisotropyEnable        = VK_FALSE;
        samplerInfo.maxAnisotropy           = 0;
        samplerInfo.compareEnable           = VK_TRUE;
        samplerInfo.compareOp               = VK_COMPARE_OP_LESS_OR_EQUAL;
        samplerInfo.minLod                  = 0;
        samplerInfo.maxLod                  = 1;
        samplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler iblIntegrationSampler = VK_NULL_HANDLE;
        CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &iblIntegrationSampler));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            32, // binding
            0,  // arrayElement
            iblIntegrationSampler);
    }

    // SamplerState                       IBLMapSampler                 : register(s33);
    {
        VkSamplerCreateInfo samplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.flags                   = 0;
        samplerInfo.magFilter               = VK_FILTER_LINEAR;
        samplerInfo.minFilter               = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.mipLodBias              = 0;
        samplerInfo.anisotropyEnable        = VK_FALSE;
        samplerInfo.maxAnisotropy           = 0;
        samplerInfo.compareEnable           = VK_TRUE;
        samplerInfo.compareOp               = VK_COMPARE_OP_LESS_OR_EQUAL;
        samplerInfo.minLod                  = 0;
        samplerInfo.maxLod                  = FLT_MAX;
        samplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler iblMapSampler = VK_NULL_HANDLE;
        CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &iblMapSampler));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            33, // binding
            0,  // arrayElement
            iblMapSampler);
    }

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}

void WriteEnvDescriptors(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pDescriptorBuffer,
    VulkanImage*          pEnvTexture)
{
    char* pDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        pRenderer->Allocator,
        pDescriptorBuffer->Allocation,
        reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // set via push constants
    // ConstantBuffer<SceneParameters> SceneParams       : register(b0);

    // SamplerState                    IBLMapSampler     : register(s1);
    {
        VkSamplerCreateInfo samplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.flags                   = 0;
        samplerInfo.magFilter               = VK_FILTER_LINEAR;
        samplerInfo.minFilter               = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.mipLodBias              = 0;
        samplerInfo.anisotropyEnable        = VK_FALSE;
        samplerInfo.maxAnisotropy           = 0;
        samplerInfo.compareEnable           = VK_TRUE;
        samplerInfo.compareOp               = VK_COMPARE_OP_NEVER;
        samplerInfo.minLod                  = 0;
        samplerInfo.maxLod                  = FLT_MAX;
        samplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler iblMapSampler = VK_NULL_HANDLE;
        CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &iblMapSampler));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            1, // binding
            0, // arrayElement
            iblMapSampler);
    }

    // Texture2D                       IBLEnvironmentMap : register(t2);
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            pEnvTexture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            GREX_ALL_SUBRESOURCES,
            &imageView));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            2, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}
