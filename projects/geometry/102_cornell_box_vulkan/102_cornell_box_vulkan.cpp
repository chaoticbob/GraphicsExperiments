#include "window.h"

#include "vk_renderer.h"
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

struct DrawInfo
{
    uint32_t      materialIndex = 0;
    uint32_t      numIndices    = 0;
    VulkanBuffer  indexBuffer;
};

struct Material
{
    glm::vec3 albedo       = glm::vec3(1);
    uint32_t  recieveLight = 1;
};

struct Camera
{
   mat4 mvp;
   vec3 lightPosition;
};

struct DrawParameters
{
   uint MaterialIndex;
};

// =============================================================================
// Shader code
// =============================================================================
const char* gShadersVS = R"(
#version 460

layout(binding=0) uniform CameraProperties
{
   mat4 MVP;
   vec3 LightPosition;
} Camera;

in vec3 PositionOS;
in vec3 Normal;

out vec3 outPositionOS;
out vec3 outNormal;

void main()
{
   gl_Position = Camera.MVP * vec4(PositionOS, 1);
   outPositionOS = PositionOS;
   outNormal = Normal;
}
)";
const char* gShadersFS = R"(
#version 460

layout(binding=0) uniform CameraProperties
{
   mat4 MVP;
   vec3 LightPosition;
} Camera;

layout(push_constant) uniform DrawParameters
{
   uint MaterialIndex;
} DrawParams;

struct Material
{
   vec3 Albedo;
   uint recieveLight;
};

layout(binding=2) buffer MaterialsStructuredBuffer
{
   Material Materials[];
};

in vec3 PositionOS;
in vec3 Normal;

out vec4 FragColor;

void main()
{
   vec3 lightDir = normalize(Camera.LightPosition - PositionOS);
   float diffuse = 0.7 * clamp(dot(lightDir, Normal), 0, 1);

   Material material = Materials[DrawParams.MaterialIndex];
   vec3 color = material.Albedo;
   if (material.recieveLight > 0) {
       color = (0.3 + diffuse) * material.Albedo;
   }

   FragColor = vec4(color, 1);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;
static bool     gEnableRayTracing = false;

void CreatePipelineLayout(VulkanRenderer* pRenderer, VulkanPipelineLayout *pLayout);
void CreateShaderModules(
   VulkanRenderer*               pRenderer,
   const std::vector<uint32_t>&  spirvVS,
   const std::vector<uint32_t>&  spirvFS,
   VkShaderModule*               pModuleVS,
   VkShaderModule*               pModuleFS);
void CreateGeometryBuffers(
   VulkanRenderer*              pRenderer,
   std::vector<DrawInfo>&       outDrawParams,
   VulkanBuffer*                pCameraBuffer,
   VulkanBuffer*                pMaterialBuffer,
   VulkanBuffer*                pPositionBuffer,
   VulkanBuffer*                pNormalBuffer,
   vec3*                        pLightPosition);
void CreateDescriptorBuffer(
   VulkanRenderer*              pRenderer,
   VkDescriptorSetLayout        descriptorSetLayout,
   VulkanBuffer*                pDescriptorBuffer);
void WriteDescriptors(
   VulkanRenderer*              pRenderer,
   VkDescriptorSetLayout        descriptorSetLayout,
   VulkanBuffer*                pDescriptorBuffer,
   VulkanBuffer*                pCameraBuffer,
   VulkanBuffer*                pMaterialBuffer);

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
    std::vector<uint32_t> spirvVS;
    std::vector<uint32_t> spirvFS;
    {
        std::string errorMsg;
        CompileResult vkRes = CompileGLSL(gShadersVS, VK_SHADER_STAGE_VERTEX_BIT, {}, &spirvVS, &errorMsg);
        if (vkRes != COMPILE_SUCCESS) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        vkRes = CompileGLSL(gShadersFS, VK_SHADER_STAGE_FRAGMENT_BIT, {}, &spirvFS, &errorMsg);
        if (vkRes != COMPILE_SUCCESS) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Pipeline layout
    //
    // This is used for pipeline creation
    //
    // *************************************************************************
    VulkanPipelineLayout pipelineLayout = {};
    CreatePipelineLayout(renderer.get(), &pipelineLayout);

    // *************************************************************************
    // Shader module
    // *************************************************************************
    VkShaderModule moduleVS = VK_NULL_HANDLE;
    VkShaderModule moduleFS = VK_NULL_HANDLE;
    CreateShaderModules(
       renderer.get(),
       spirvVS,
       spirvFS,
       &moduleVS,
       &moduleFS);

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    VkPipeline pipeline = VK_NULL_HANDLE;
    CHECK_CALL(CreateDrawNormalPipeline(
       renderer.get(),
       pipelineLayout.PipelineLayout,
       moduleVS,
       moduleFS,
       GREX_DEFAULT_RTV_FORMAT,
       GREX_DEFAULT_DSV_FORMAT,
       &pipeline));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    std::vector<DrawInfo>  drawParams;
    VulkanBuffer           cameraBuffer;
    VulkanBuffer           materialsBuffer;
    VulkanBuffer           positionBuffer;
    VulkanBuffer           normalBuffer;
    vec3                   lightPosition;
    CreateGeometryBuffers(
       renderer.get(),
       drawParams,
       &cameraBuffer,
       &materialsBuffer,
       &positionBuffer,
       &normalBuffer,
       &lightPosition);

   // *************************************************************************
   // Descriptor heaps
   // *************************************************************************
   VulkanBuffer descriptorBuffer = {};
   CreateDescriptorBuffer(renderer.get(), pipelineLayout.DescriptorSetLayout, &descriptorBuffer);

   WriteDescriptors(
      renderer.get(),
      pipelineLayout.DescriptorSetLayout,
      &descriptorBuffer,
      &cameraBuffer,
      &materialsBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "102_cornell_box_vulkan");
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight())) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain image views, depth buffers/views
    // *************************************************************************
    std::vector<VkImage>     images;
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
    // Persistent map parameters
    // *************************************************************************
    Camera* pCameraParams = nullptr;
    CHECK_CALL(vmaMapMemory(renderer->Allocator, cameraBuffer.Allocation, reinterpret_cast<void**>(&pCameraParams)));

   // *************************************************************************
   // Persistent map descriptor buffer
   // *************************************************************************
   char* pDescriptorBufferStartAddress = nullptr;
   CHECK_CALL(vmaMapMemory(
      renderer->Allocator,
      descriptorBuffer.Allocation,
      reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color = { {0.0f, 0.0f, 0.2f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    while (window->PollEvents()) {
        uint32_t bufferIndex = 0;
        if (AcquireNextImage(renderer.get(), &bufferIndex)) {
            assert(false && "AcquireNextImage failed");
            break;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        {
           CmdTransitionImageLayout(
              cmdBuf.CommandBuffer,
              images[bufferIndex],
              GREX_ALL_SUBRESOURCES,
              VK_IMAGE_ASPECT_COLOR_BIT,
              RESOURCE_STATE_PRESENT,
              RESOURCE_STATE_RENDER_TARGET);

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

           // Bind the VS/FS Graphics Pipeline
           vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

           VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT };
           descriptorBufferBindingInfo.pNext                            = nullptr;
           descriptorBufferBindingInfo.address                          = GetDeviceAddress(renderer.get(), &descriptorBuffer);
           descriptorBufferBindingInfo.usage                            = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

           fn_vkCmdBindDescriptorBuffersEXT(cmdBuf.CommandBuffer, 1, &descriptorBufferBindingInfo);

           uint32_t     bufferIndices           = 0;
           VkDeviceSize descriptorBufferOffsets = 0;
           fn_vkCmdSetDescriptorBufferOffsetsEXT(
              cmdBuf.CommandBuffer,
              VK_PIPELINE_BIND_POINT_GRAPHICS,
              pipelineLayout.PipelineLayout,
              0, // firstSet
              1, // setCount
              &bufferIndices,
              &descriptorBufferOffsets);

           // Bind the Vertex Buffer
           VkBuffer vertexBuffers[] = { positionBuffer.Buffer, normalBuffer.Buffer };
           VkDeviceSize offsets[] = { 0, 0 };
           vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 2, vertexBuffers, offsets);

            mat4 modelMat = mat4(1);
            mat4 viewMat  = glm::lookAt(vec3(0, 3, 5), vec3(0, 2.8f, 0), vec3(0, 1, 0));
            mat4 projMat  = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
            mat4 mvpMat   = projMat * viewMat * modelMat;

            int32 cameraOffsetInBytes     = 0;
            int32 materialOffsetInBytes   = sizeof(Camera);
            int32 materialsOffsetInBytes  = sizeof(Camera) + sizeof(Material);

            pCameraParams->mvp            = mvpMat;
            pCameraParams->lightPosition  = lightPosition;

            for (auto& draw : drawParams) {
               // Bind the index buffer for this draw
               vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, draw.indexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

               vkCmdPushConstants(
                  cmdBuf.CommandBuffer,
                  pipelineLayout.PipelineLayout,
                  VK_SHADER_STAGE_FRAGMENT_BIT,
                  0,
                  sizeof(DrawParameters),
                  &draw.materialIndex);

               vkCmdDrawIndexed(cmdBuf.CommandBuffer, draw.numIndices, 1, 0, 0, 0);
            }

            vkCmdEndRendering(cmdBuf.CommandBuffer);

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

void CreatePipelineLayout(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout)
{
   // Descriptor set layout
   {
      std::vector<VkDescriptorSetLayoutBinding> bindings = {};
      // layout(binding=0) uniform CameraProperties Camera;
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 0;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
         bindings.push_back(binding);
      }
      // layout(binding=2) buffer MaterialStructuredBuffer
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 2;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;
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

   // Pipeline layout
   {
      // layout(push_constants) uniform MaterialParameters MaterialParams;
      VkPushConstantRange push_constant    = {};
      push_constant.offset                 = 0;
      push_constant.size                   = sizeof(DrawParameters);
      push_constant.stageFlags             = VK_SHADER_STAGE_FRAGMENT_BIT;

      VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      createInfo.setLayoutCount             = 1;
      createInfo.pSetLayouts                = &pLayout->DescriptorSetLayout;
      createInfo.pushConstantRangeCount     = 1;
      createInfo.pPushConstantRanges        = &push_constant;

      CHECK_CALL(vkCreatePipelineLayout(
         pRenderer->Device,
         &createInfo,
         nullptr,
         &pLayout->PipelineLayout));
   }
}

void CreateShaderModules(
   VulkanRenderer*               pRenderer,
   const std::vector<uint32_t>&  spirvVS,
   const std::vector<uint32_t>&  spirvFS,
   VkShaderModule*               pModuleVS,
   VkShaderModule*               pModuleFS)
{
   // Vertex Shader
   {
      VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      createInfo.codeSize                 = SizeInBytes(spirvVS);
      createInfo.pCode                    = DataPtr(spirvVS);

      CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleVS));
   }

   // Fragment Shader
   {
      VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      createInfo.codeSize                 = SizeInBytes(spirvFS);
      createInfo.pCode                    = DataPtr(spirvFS);

      CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleFS));
   }
}

void CreateGeometryBuffers(
   VulkanRenderer*        pRenderer,
   std::vector<DrawInfo>& outDrawParams,
   VulkanBuffer*          pCameraBuffer,
   VulkanBuffer*          pMaterialBuffer,
   VulkanBuffer*          pPositionBuffer,
   VulkanBuffer*          pNormalBuffer,
   vec3*                  pLightPosition)
{
   TriMesh mesh = TriMesh::CornellBox({ .enableVertexColors = true, .enableNormals = true });

   uint32_t lightGroupIndex = mesh.GetGroupIndex("light");
   assert((lightGroupIndex != UINT32_MAX) && "group index for 'light' failed");

   *pLightPosition = mesh.GetGroup(lightGroupIndex).GetBounds().Center();

   std::vector<Material> materials;
   for (uint32_t materialIndex = 0; materialIndex < mesh.GetNumMaterials(); ++materialIndex) {
      auto& matDesc = mesh.GetMaterial(materialIndex);

      Material material     = {};
      material.albedo       = matDesc.baseColor;
      material.recieveLight = (matDesc.name != "white light") ? true : false;
      materials.push_back(material);

      auto triangles = mesh.GetTrianglesForMaterial(materialIndex);

      DrawInfo params = {};
      params.numIndices     = static_cast<uint32_t>(3 * triangles.size());
      params.materialIndex  = materialIndex;

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(triangles),
         DataPtr(triangles),
         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
         VMA_MEMORY_USAGE_GPU_ONLY,
         0,
         &params.indexBuffer));

      outDrawParams.push_back(params);
   }

   CHECK_CALL(CreateBuffer(
      pRenderer,
      Align<size_t>(sizeof(Camera), 256),
      nullptr,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      0,
      pCameraBuffer));

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(materials),
      DataPtr(materials),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY,
      0,
      pMaterialBuffer));

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
}

void CreateDescriptorBuffer(
   VulkanRenderer*              pRenderer,
   VkDescriptorSetLayout        descriptorSetLayout,
   VulkanBuffer*                pDescriptorBuffer)
{
   VkDeviceSize size = 256;
   fn_vkGetDescriptorSetLayoutSizeEXT(pRenderer->Device, descriptorSetLayout, &size);

   VkBufferUsageFlags usageFlags =
      VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

   CHECK_CALL(CreateBuffer(
      pRenderer,           // pRenderer
      size,                // srcSize
      nullptr,             // pSrcData
      usageFlags,          // usageFlags
      0,                   // minAlignment
      pDescriptorBuffer)); // pBuffer
}

void WriteDescriptors(
   VulkanRenderer*        pRenderer,
   VkDescriptorSetLayout  descriptorSetLayout,
   VulkanBuffer*          pDescriptorBuffer,
   VulkanBuffer*          pCameraBuffer,
   VulkanBuffer*          pMaterialBuffer)
{
   char* pDescriptorBufferStartAddress = nullptr;

   CHECK_CALL(vmaMapMemory(
      pRenderer->Allocator,
      pDescriptorBuffer->Allocation,
      reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    WriteDescriptor(
       pRenderer,
       pDescriptorBufferStartAddress,
       descriptorSetLayout,
       0, // binding
       0, // arrayElement
       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       pCameraBuffer);

    WriteDescriptor(
       pRenderer,
       pDescriptorBufferStartAddress,
       descriptorSetLayout,
       2, // binding
       0, // arrayElement
       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       pMaterialBuffer);

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}