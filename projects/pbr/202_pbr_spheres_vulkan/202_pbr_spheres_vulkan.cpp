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

struct PBRSceneParameters
{
   mat4     viewProjectionMatrix;
   vec3     eyePosition;
   uint32_t numLights;
   Light    lights[8];
   uint     iblEnvironmentNumLevels;
};

struct EnvSceneParameters
{
   mat4     MVP;
};

struct DrawParameters
{ 
   mat4     ModelMatrix;
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

static uint32_t gNumSlotsX     = 10;
static uint32_t gNumSlotsY     = 10;
static float    gSlotSize      = 0.9f;
static float    gSpanX         = gNumSlotsX * gSlotSize;
static float    gSpanY         = gNumSlotsY * gSlotSize;
static float    gHalfSpanX     = gSpanX / 2.0f;
static float    gHalfSpanY     = gSpanY / 2.0f;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static uint32_t gNumLights = 0;


void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout);
void CreateEnvironmentPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout);
void CreateSphereParamBuffers(
   VulkanRenderer* pRenderer, 
   std::vector<VulkanBuffer> &materialParamBuffers,
   std::vector<VulkanBuffer> &drawParamBuffers);
void CreateMaterialSphereVertexBuffers(
    VulkanRenderer*  pRenderer,
    uint32_t*        pNumIndices,
    VulkanBuffer*    pIndexBuffer,
    VulkanBuffer*    pPositionBuffer,
    VulkanBuffer*    pNormalBuffer);
void CreateEnvironmentVertexBuffers(
    VulkanRenderer*  pRenderer,
    uint32_t*        pNumIndices,
    VulkanBuffer*    pIndexBuffer,
    VulkanBuffer*    pPositionBuffer,
    VulkanBuffer*    pTexCoordBuffer);
void CreateIBLTextures(
    VulkanRenderer*  pRenderer,
    VulkanImage*     pBRDFLUT,
    VulkanImage*     pIrradianceTexture,
    VulkanImage*     pEnvironmentTexture,
    uint32_t*        pEnvNumLevels);
void CreateDescriptorBuffer(
   VulkanRenderer*         pRenderer,
   VkDescriptorSetLayout   pDescriptorSetLayout,
   VulkanBuffer*           pBuffer);
void WritePBRDescriptors(
   VulkanRenderer*         pRenderer,
   VkDescriptorSetLayout   descriptorSetLayout,
   VulkanBuffer*           pDescriptorBuffer,
   const VulkanBuffer*     pSceneParamsBuffer,
   const VulkanImage*      pBRDFLUT,
   const VulkanImage*      pIrradianceTexture,
   const VulkanImage*      pEnvTexture);
void WriteEnvDescriptors(
   VulkanRenderer*         pRenderer,
   VkDescriptorSetLayout   descriptorSetLayout,
   VulkanBuffer*           pDescrptorBuffer,
   VulkanBuffer*           pSceneParamsBuffer,
   VulkanImage*            pEnvTexture);

/*
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
       createInfo.codeSize                   = SizeInBytes(drawTextureSpirvVS);
       createInfo.pCode                      = DataPtr(drawTextureSpirvVS);

       CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &drawTextureShaderModuleVS));
    }

    VkShaderModule drawTextureShaderModuleFS = VK_NULL_HANDLE;
    {
       VkShaderModuleCreateInfo createInfo   = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
       createInfo.codeSize                   = SizeInBytes(drawTextureSpirvFS);
       createInfo.pCode                      = DataPtr(drawTextureSpirvFS);

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

    VulkanBuffer envSceneParamsBuffer;
    CHECK_CALL(CreateBuffer(
       renderer.get(),
       Align<size_t>(sizeof(EnvSceneParameters), 256),
       nullptr,
       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
       VMA_MEMORY_USAGE_CPU_TO_GPU,
       0,
       &envSceneParamsBuffer));

    // *************************************************************************
    // Material Params Buffer
    // *************************************************************************
    std::vector<VulkanBuffer> materialParamBuffers;
    std::vector<VulkanBuffer> drawParamBuffers;
    CreateSphereParamBuffers(renderer.get(), materialParamBuffers, drawParamBuffers);

    // *************************************************************************
    // Material sphere vertex buffers
    // *************************************************************************
    uint32_t     materialSphereNumIndices = 0;
    VulkanBuffer materialSphereIndexBuffer;
    VulkanBuffer materialSpherePositionBuffer;
    VulkanBuffer materialSphereNormalBuffer;
    CreateMaterialSphereVertexBuffers(
        renderer.get(),
        &materialSphereNumIndices,
        &materialSphereIndexBuffer,
        &materialSpherePositionBuffer,
        &materialSphereNormalBuffer);

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
    VulkanImage irrTexture;
    VulkanImage envTexture;
    uint32_t    envNumLevels = 0;
    CreateIBLTextures(renderer.get(), &brdfLUT, &irrTexture, &envTexture, &envNumLevels);

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
       &brdfLUT,
       &irrTexture,
       &envTexture);

    VulkanBuffer envDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), envPipelineLayout.DescriptorSetLayout, &envDescriptorBuffer);

    WriteEnvDescriptors(
       renderer.get(),
       envPipelineLayout.DescriptorSetLayout,
       &envDescriptorBuffer,
       &envSceneParamsBuffer,
       &envTexture);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "202_pbr_spheres_vulkan");
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
    std::vector<VkImageView>  imageViews;
    std::vector<VkImageView>  depthViews;
    {
       std::vector<VkImage>      images;
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

       std::vector<VulkanImage> depthImages;
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

    EnvSceneParameters* pEnvSceneParams = nullptr;
    vmaMapMemory(renderer->Allocator, envSceneParamsBuffer.Allocation, reinterpret_cast<void**>(&pEnvSceneParams));

    char* pPBRDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
       renderer->Allocator,
       pbrDescriptorBuffer.Allocation,
       reinterpret_cast<void**>(&pPBRDescriptorBufferStartAddress)));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color = { {0.0f, 0.0f, 0.2f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    while (window->PollEvents()) {
       window->ImGuiNewFrameVulkan();

       if (ImGui::Begin("Scene")) {
          ImGui::SliderInt("Number of Lights", reinterpret_cast<int*>(&gNumLights), 0, 4);
       }
       ImGui::End();

       // ---------------------------------------------------------------------

       UINT bufferIndex = 0;
       if (AcquireNextImage(renderer.get(), &bufferIndex)) {
          assert(false && "AcquireNextImage failed");
          break;
       }

       VkCommandBufferBeginInfo vkbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
       vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

       CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

       {
          VkRenderingAttachmentInfo colorAttachment  = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
          colorAttachment.imageView                  = imageViews[bufferIndex];
          colorAttachment.imageLayout                = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
          colorAttachment.loadOp                     = VK_ATTACHMENT_LOAD_OP_CLEAR;
          colorAttachment.storeOp                    = VK_ATTACHMENT_STORE_OP_STORE;
          colorAttachment.clearValue                 = clearValues[0];

          VkRenderingAttachmentInfo depthAttachment  = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
          depthAttachment.imageView                  = depthViews[bufferIndex];
          depthAttachment.imageLayout                = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
          depthAttachment.loadOp                     = VK_ATTACHMENT_LOAD_OP_CLEAR;
          depthAttachment.storeOp                    = VK_ATTACHMENT_STORE_OP_DONT_CARE;
          depthAttachment.clearValue                 = clearValues[1];

          VkRenderingInfo vkri                       = { VK_STRUCTURE_TYPE_RENDERING_INFO };
          vkri.layerCount                            = 1;
          vkri.colorAttachmentCount                  = 1;
          vkri.pColorAttachments                     = &colorAttachment;
          vkri.pDepthAttachment                      = &depthAttachment;
          vkri.renderArea.extent.width               = gWindowWidth;
          vkri.renderArea.extent.height              = gWindowHeight;

          vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);

          VkViewport viewport = { 0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f };
          vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

          VkRect2D scissor = { 0, 0, gWindowWidth, gWindowHeight };
          vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

          // Smooth out the rotation on Y
          gAngle += (gTargetAngle - gAngle) * 0.1f;

          // Camera matrices
          vec3 eyePosition = vec3(0, 0, 9);
          mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
          mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
          mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));

          // Set constant buffer values
          pPBRSceneParams->viewProjectionMatrix    = projMat * viewMat;
          pPBRSceneParams->eyePosition             = eyePosition;
          pPBRSceneParams->numLights               = gNumLights;
          pPBRSceneParams->lights[0].position      = vec3(5, 7, 32);
          pPBRSceneParams->lights[0].color         = vec3(0.98f, 0.85f, 0.71f);
          pPBRSceneParams->lights[0].intensity     = 0.5f;
          pPBRSceneParams->lights[1].position      = vec3(-8, 1, 4);
          pPBRSceneParams->lights[1].color         = vec3(1.00f, 0.00f, 0.00f);
          pPBRSceneParams->lights[1].intensity     = 0.5f;
          pPBRSceneParams->lights[2].position      = vec3(0, 8, -8);
          pPBRSceneParams->lights[2].color         = vec3(0.00f, 1.00f, 0.00f);
          pPBRSceneParams->lights[2].intensity     = 0.5f;
          pPBRSceneParams->lights[3].position      = vec3(15, 8, 0);
          pPBRSceneParams->lights[3].color         = vec3(0.00f, 0.00f, 1.00f);
          pPBRSceneParams->lights[3].intensity     = 0.5f;
          pPBRSceneParams->iblEnvironmentNumLevels = envNumLevels;

          // Draw environment
          {
             // Bind the VS/FS Graphics Pipeline
             vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, envPipelineState);

             glm::mat4 moveUp = glm::translate(vec3(0, 0, 0));

             // SceneParmas (b0)
             mat4 mvp = projMat * viewMat * moveUp;
             pEnvSceneParams->MVP = mvp;

             // Bind the Index Buffer
             vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, envIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

             // Bind the Vertex Buffer
             VkBuffer vertexBuffers[] = { envPositionBuffer.Buffer, envTexCoordBuffer.Buffer };
             VkDeviceSize offsets[] = { 0, 0 };
             vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 2, vertexBuffers, offsets);

             vkCmdDrawIndexed(cmdBuf.CommandBuffer, envNumIndices, 1, 0, 0, 0);
          }

          // Draw material sphere
          {
             // Bind the Index Buffer
             vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, materialSphereIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

             // Bind the Vertex Buffer
             VkBuffer vertexBuffers[] = { materialSpherePositionBuffer.Buffer, materialSphereNormalBuffer.Buffer };
             VkDeviceSize offsets[] = { 0, 0 };
             vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 2, vertexBuffers, offsets);

             // Pipeline state
             vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipelineState);

             for (uint32_t i = 0; i < gNumSlotsY; ++i) {
                for (uint32_t j = 0; j < gNumSlotsX; ++j) {
                   uint32_t sphereIndex= i*gNumSlotsX + j;

                   float x = -gHalfSpanX + j * gSlotSize;
                   float y = -gHalfSpanY + i * gSlotSize;
                   float z = 0;
                   // Readjust center
                   x += gSlotSize / 2.0f;
                   y += gSlotSize / 2.0f;

                   // DrawParams (b1)
                   {
                      WriteDescriptor(
                         renderer.get(),
                         pPBRDescriptorBufferStartAddress,
                         pbrPipelineLayout.DescriptorSetLayout,
                         1, // binding
                         0, // arrayElement
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                         &drawParamBuffers[sphereIndex]);
                      
                      // Modify the buffer to have this frame's current rotation
                      DrawParameters* pDrawParams = nullptr;
                      vmaMapMemory(
                         renderer->Allocator,
                         drawParamBuffers[sphereIndex].Allocation,
                         reinterpret_cast<void**>(&pDrawParams));

                      pDrawParams->ModelMatrix = rotMat * glm::translate(vec3(x, y, z));

                      vmaUnmapMemory(renderer->Allocator, drawParamBuffers[sphereIndex].Allocation);
                   }

                   // MaterialParams (b2)
                   {
                      WriteDescriptor(
                         renderer.get(),
                         pPBRDescriptorBufferStartAddress,
                         pbrPipelineLayout.DescriptorSetLayout,
                         2, // binding
                         0, // arrayElement
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                         &materialParamBuffers[sphereIndex]);
                   }

                   vkCmdDrawIndexed(cmdBuf.CommandBuffer, materialSphereNumIndices, 1, 0, 0, 0);
                }
             }
          }

          // Draw ImGui
          window->ImGuiRenderDrawData(renderer.get(), cmdBuf.CommandBuffer);

          vkCmdEndRendering(cmdBuf.CommandBuffer);
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
         binding.binding                      = 1;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_ALL;
         binding.pImmutableSamplers           = &uWrapSampler;

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

void CreateSphereParamBuffers(
   VulkanRenderer* pRenderer,
   std::vector<VulkanBuffer> &materialParamBuffers,
   std::vector<VulkanBuffer> &drawParamBuffers)
{
   MaterialParameters materialParams = {};
   materialParams.albedo             = vec3(0.8f, 0.8f, 0.9f);
   materialParams.roughness          = 0;
   materialParams.metalness          = 0;
   materialParams.F0                 = F0_Generic;

   float    roughnessStep = 1.0f / (gNumSlotsX - 1);
   float    metalnessStep = 1.0f / (gNumSlotsY - 1);

   for (uint32_t i = 0; i < gNumSlotsY; ++i) {
      materialParams.metalness = 0;

      for (uint32_t j = 0; j < gNumSlotsX; ++j) {
         float x = -gHalfSpanX + j * gSlotSize;
         float y = -gHalfSpanY + i * gSlotSize;
         float z = 0;
         // Readjust center
         x += gSlotSize / 2.0f;
         y += gSlotSize / 2.0f;

         // Create the DrawParameters for this sphere
         {
            VulkanBuffer drawParamsBuffer = {};

            CHECK_CALL(CreateBuffer(
               pRenderer,
               Align<size_t>(sizeof(DrawParameters), 256),
               nullptr, // Can't set the values because they change per frame
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               0,
               &drawParamsBuffer));

            drawParamBuffers.push_back(drawParamsBuffer);
         }

         // Create the MaterialParameters for this sphere
         {
            VulkanBuffer materialParamsBuffer = {};

            CHECK_CALL(CreateBuffer(
               pRenderer,
               Align<size_t>(sizeof(MaterialParameters), 256),
               &materialParams,
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               0,
               &materialParamsBuffer));

            materialParamBuffers.push_back(materialParamsBuffer);
         }

         materialParams.metalness += roughnessStep;
      }

      materialParams.roughness += metalnessStep;
   }
}

void CreateMaterialSphereVertexBuffers(
    VulkanRenderer*  pRenderer,
    uint32_t*        pNumIndices,
    VulkanBuffer*    pIndexBuffer,
    VulkanBuffer*    pPositionBuffer,
    VulkanBuffer*    pNormalBuffer)
{
   TriMesh mesh = TriMesh::Sphere(0.42f, 256, 256, { .enableNormals = true });

   *pNumIndices = 3 * mesh.GetNumTriangles();

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(mesh.GetTriangles()),
      DataPtr(mesh.GetTriangles()),
      VMA_MEMORY_USAGE_GPU_ONLY,
      0,
      pIndexBuffer));

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(mesh.GetPositions()),
      DataPtr(mesh.GetPositions()),
      VMA_MEMORY_USAGE_GPU_ONLY,
      0,
      pPositionBuffer));

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(mesh.GetNormals()),
      DataPtr(mesh.GetNormals()),
      VMA_MEMORY_USAGE_GPU_ONLY,
      0,
      pNormalBuffer));
}

void CreateEnvironmentVertexBuffers(
    VulkanRenderer*  pRenderer,
    uint32_t*        pNumIndices,
    VulkanBuffer*    pIndexBuffer,
    VulkanBuffer*    pPositionBuffer,
    VulkanBuffer*    pTexCoordBuffer)
{
   TriMesh mesh = TriMesh::Sphere(100, 64, 64, { .enableTexCoords = true, .faceInside = true });

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
    VulkanRenderer*  pRenderer,
    VulkanImage*     pBRDFLUT,
    VulkanImage*     pIrradianceTexture,
    VulkanImage*     pEnvironmentTexture,
    uint32_t*        pEnvNumLevels)
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

      std::vector<VkMipOffset> mipOffsets;
      uint32_t                 levelOffset = 0;
      uint32_t                 levelWidth  = ibl.baseWidth;
      uint32_t                 levelHeight = ibl.baseHeight;
      for (uint32_t i = 0; i < ibl.numLevels; ++i) {
         VkMipOffset mipOffset = {};
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
         VK_FORMAT_R32G32B32A32_SFLOAT,
         mipOffsets,
         ibl.environmentMap.GetSizeInBytes(),
         ibl.environmentMap.GetPixels(),
         pEnvironmentTexture));
   }

   GREX_LOG_INFO("Loaded " << iblFile);
}

void CreateDescriptorBuffer(
   VulkanRenderer*         pRenderer,
   VkDescriptorSetLayout   descriptorSetLayout,
   VulkanBuffer*           pBuffer)
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
   VulkanRenderer*         pRenderer,
   VkDescriptorSetLayout   descriptorSetLayout,
   VulkanBuffer*           pDescriptorBuffer,
   const VulkanBuffer*     pSceneParamsBuffer,
   const VulkanImage*      pBRDFLUT,
   const VulkanImage*      pIrradianceTexture,
   const VulkanImage*      pEnvTexture)
{
   char* pDescriptorBufferStartAddress = nullptr;
   CHECK_CALL(vmaMapMemory(
      pRenderer->Allocator,
      pDescriptorBuffer->Allocation,
      reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

   // ConstantBuffer<SceneParameters>    SceneParams           : register(b0);
   WriteDescriptor(
      pRenderer,
      pDescriptorBufferStartAddress,
      descriptorSetLayout,
      0, // binding
      0, // arrayElement
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      pSceneParamsBuffer);

   // Set per draw call
   // ConstantBuffer<DrawParameters>     DrawParams            : register(b1);
   // ConstantBuffer<MaterialParameters> MaterialParams        : register(b2);

   // Texture2D                          IBLIntegrationLUT     : register(t3);
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

   // Texture2D                          IBLIrradianceMap      : register(t4);
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
         4, // binding
         0, // arrayElement
         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         imageView,
         VK_IMAGE_LAYOUT_GENERAL);
   }

   // Texture2D                          IBLEnvironmentMap     : register(t5);
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
         5, // binding
         0, // arrayElement
         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         imageView,
         VK_IMAGE_LAYOUT_GENERAL);
   }

   // Samplers are setup in the immutable samplers in the DescriptorSetLayout
   // SamplerState                       IBLIntegrationSampler : register(s6);
   // SamplerState                       IBLMapSampler         : register(s7);

   vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}

void WriteEnvDescriptors(
   VulkanRenderer*         pRenderer,
   VkDescriptorSetLayout   descriptorSetLayout,
   VulkanBuffer*           pDescriptorBuffer,
   VulkanBuffer*           pSceneParamsBuffer,
   VulkanImage*            pEnvTexture)
{
   char* pDescriptorBufferStartAddress = nullptr;
   CHECK_CALL(vmaMapMemory(
      pRenderer->Allocator,
      pDescriptorBuffer->Allocation,
      reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

   // ConstantBuffer<SceneParameters> SceneParams       : register(b0);
   WriteDescriptor(
      pRenderer,
      pDescriptorBufferStartAddress,
      descriptorSetLayout,
      0, // binding
      0, // arrayElement
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      pSceneParamsBuffer);

   // Sampler is setup in the immutable samplers in the DescriptorSetLayout
   // SamplerState                    IBLMapSampler     : register(s1);

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