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
        VkResult vkres = FN;                         \
        if (vkres != VK_SUCCESS) {                   \
            std::stringstream ss;                    \
            ss << "\n";                              \
            ss << "*** FUNCTION CALL FAILED *** \n"; \
            ss << "FUNCTION: " << #FN << "\n";       \
            ss << "\n";                              \
            GREX_LOG_ERROR(ss.str().c_str());        \
            assert(false);                           \
        }                                            \
    }

// =============================================================================
// Shader code
// =============================================================================
const char* gShaderVS= R"(
#version 460

layout( push_constant ) uniform CameraProperties 
{
	mat4 MVP;
} cam;

in vec3 PositionOS;
in vec3 Color;

out vec3 vertexColor;	// Specify a color output to the fragment shader

void main()
{
	gl_Position = cam.MVP * vec4(PositionOS, 1);
	vertexColor = Color;
}
)";

const char* gShaderFS= R"(
#version 460

in vec3 vertexColor;	// The input variable from the vertex shader (of the same name)

out vec4 FragColor;

void main()
{
	FragColor = vec4(vertexColor, 1.0f);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth        = 1280;
static uint32_t gWindowHeight       = 720;
static bool     gEnableDebug        = true;
static bool     gEnableRayTracing   = false;
static uint32_t gUniformmBufferSize = 256;

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkPipelineLayout* pLayout);
void CreateRenderPass(VulkanRenderer* pRenderer, VkRenderPass* pRenderPass);
void CreateShaderModules(
   VulkanRenderer*               pRenderer,
   const std::vector<uint32_t>&  spirvVS,
   const std::vector<uint32_t>&  spirvFS,
   VkShaderModule*               pModuleVS,
   VkShaderModule*               pModuleFS);
void CreateGeometryBuffers(
   VulkanRenderer*   pRenderer,
   VulkanBuffer*     ppIndexBuffer,
   VulkanBuffer*     ppPositionBuffer,
   VulkanBuffer*     ppVertexColorBuffer);

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
    //
    // Make sure the shaders compile before we do anything.
    //
    // *************************************************************************
	std::vector<uint32_t> spirvVS;
	std::vector<uint32_t> spirvFS;
   {
      std::string   errorMsg;
      CompileResult res = CompileGLSL(gShaderVS, "main", VK_SHADER_STAGE_VERTEX_BIT, {}, &spirvVS, &errorMsg);
      if (res != COMPILE_SUCCESS) {
         std::stringstream ss;
         ss << "\n"
            << "Shader compiler error (VS): " << errorMsg << "\n";
         GREX_LOG_ERROR(ss.str().c_str());
         return EXIT_FAILURE;
      }

      res = CompileGLSL(gShaderFS, "main", VK_SHADER_STAGE_FRAGMENT_BIT, {}, &spirvFS, &errorMsg);
      if (res != COMPILE_SUCCESS) {
         std::stringstream ss;
         ss << "\n"
            << "Shader compiler error (VS): " << errorMsg << "\n";
         GREX_LOG_ERROR(ss.str().c_str());
         return EXIT_FAILURE;
      }
   }

    // *************************************************************************
    // Pipeline layout
    //
    // This is used for pipeline creation
    //
    // *************************************************************************
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    CreatePipelineLayout(renderer.get(), &pipelineLayout);

    // *************************************************************************
    // RenderPass
    // 
    // This is used for pipeline creation
    //
    // *************************************************************************
    VkRenderPass renderPass = VK_NULL_HANDLE;
    CreateRenderPass(renderer.get(), &renderPass);

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
    // Create the pipeline
    //
    // The pipeline is created with 2 shaders
    //    1) Vertex Shader
    //    2) Fragment Shader
    //
    // *************************************************************************
    VkPipeline pipeline = VK_NULL_HANDLE;
    CreateDrawVertexColorPipeline(
       renderer.get(),
       pipelineLayout,
       renderPass,
       moduleVS,
       moduleFS,
       GREX_DEFAULT_RTV_FORMAT,
       GREX_DEFAULT_DSV_FORMAT,
       &pipeline);

    // *************************************************************************
    // Get descriptor buffer properties
    // *************************************************************************
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    {
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &descriptorBufferProperties;
        vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
    }

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    VulkanBuffer indexBuffer;
    VulkanBuffer positionBuffer;
    VulkanBuffer vertexColorBuffer;
    CreateGeometryBuffers(renderer.get(), &indexBuffer, &positionBuffer, &vertexColorBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "102_color_cube_vulkan");
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
    // Swapchain image views, depth buffers/views, and framebuffers
    // *************************************************************************
    std::vector<VkImageView> imageViews;
    std::vector<VkImageView> depthViews;
    std::vector<VkFramebuffer> framebuffers;
    {
        std::vector<VkImage> images;
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

        for (int fbIndex = 0; fbIndex < images.size(); fbIndex++) {
           // Create framebuffer object
           VkFramebufferCreateInfo createInfo         = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
           createInfo.renderPass                      = renderPass;
           createInfo.width                           = window->GetWidth();
           createInfo.height                          = window->GetHeight();
           createInfo.layers                          = 1;
           createInfo.renderPass                      = renderPass;
           createInfo.attachmentCount                 = 2;

           VkImageView attachments[] = { imageViews[fbIndex], depthViews[fbIndex] };
           createInfo.pAttachments                    = attachments;

           VkFramebuffer framebuffer = VK_NULL_HANDLE;
           CHECK_CALL(vkCreateFramebuffer(renderer->Device, &createInfo, nullptr, &framebuffer));

           framebuffers.push_back(framebuffer);
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
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color = { {0.0f, 0.0f, 0.2f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };


    while (window->PollEvents()) {
        uint32_t imageIndex = 0;
        if (AcquireNextImage(renderer.get(), &imageIndex)) {
            assert(false && "AcquireNextImage failed");
            break;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        {
           VkRenderPassBeginInfo rpbi      = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
           rpbi.renderPass                 = renderPass;
           rpbi.framebuffer                = framebuffers[imageIndex];
           rpbi.renderArea.extent.width    = gWindowWidth;
           rpbi.renderArea.extent.height   = gWindowHeight;
           rpbi.clearValueCount            = 2;
           rpbi.pClearValues               = clearValues;

           vkCmdBeginRenderPass(cmdBuf.CommandBuffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

           VkViewport viewport = { 0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f };
           vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

           VkRect2D scissor = { 0, 0, gWindowWidth, gWindowHeight };
           vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

           // Bind the VS/FS Graphics Pipeline
           vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

           // Bind the mesh vertex/index buffers
           vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, indexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

           VkBuffer vertexBuffers[] = { positionBuffer.Buffer, vertexColorBuffer.Buffer };
           VkDeviceSize offsets[] = { 0, 0 };
           vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 2, vertexBuffers, offsets);

           // Update the camera model view projection matrix
           mat4 modelMat = rotate(static_cast<float>(glfwGetTime()), vec3(0, 1, 0)) *
                           rotate(static_cast<float>(glfwGetTime()), vec3(1, 0, 0));
           mat4 viewMat = lookAt(vec3(0, 0, 2), vec3(0, 0, 0), vec3(0, 1, 0));
           mat4 projMat = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

           mat4 mvpMat  = projMat * viewMat * modelMat;

           vkCmdPushConstants(cmdBuf.CommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &mvpMat);

           vkCmdDrawIndexed(cmdBuf.CommandBuffer, 36, 1, 0, 0, 0);

           vkCmdEndRenderPass(cmdBuf.CommandBuffer);
        }

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        // Execute command buffer
        CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

        // Wait for the GPU to finish the work
        if (!WaitForGpu(renderer.get())) {
            assert(false && "WaitForGpu failed");
        }

        if (!SwapchainPresent(renderer.get(), imageIndex)) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkPipelineLayout* pLayout)
{
   VkPushConstantRange push_constant      = {};
   push_constant.offset                   = 0;
   push_constant.size                     = sizeof(mat4);
   push_constant.stageFlags               = VK_SHADER_STAGE_VERTEX_BIT;

   VkPipelineLayoutCreateInfo createInfo  = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   createInfo.pushConstantRangeCount      = 1;
   createInfo.pPushConstantRanges         = &push_constant;

   CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, pLayout));
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
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvVS);
        createInfo.pCode                    = DataPtr(spirvVS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleVS));
    }

    // Fragment Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvFS);
        createInfo.pCode                    = DataPtr(spirvFS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleFS));
    }
}

void CreateRenderPass(VulkanRenderer* pRenderer, VkRenderPass* pRenderPass)
{
   VkAttachmentDescription attachments[2] = {};
   attachments[0].format                                             = GREX_DEFAULT_RTV_FORMAT;
   attachments[0].samples                                            = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp                                             = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[0].storeOp                                            = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].initialLayout                                      = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout                                        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

   attachments[1].format                                             = GREX_DEFAULT_DSV_FORMAT;
   attachments[1].samples                                            = VK_SAMPLE_COUNT_1_BIT;
   attachments[1].loadOp                                             = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[1].storeOp                                            = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].initialLayout                                      = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[1].finalLayout                                        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   VkAttachmentReference color_reference                             = {};
   color_reference.attachment                                        = 0;
   color_reference.layout                                            = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference depth_reference                             = {};
   depth_reference.attachment                                        = 1;
   depth_reference.layout                                            = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   VkSubpassDescription subpass_description                          = {};
   subpass_description.pipelineBindPoint                             = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass_description.colorAttachmentCount                          = 1;
   subpass_description.pColorAttachments                             = &color_reference;
   subpass_description.pDepthStencilAttachment                       = &depth_reference;

   VkSubpassDependency dependencies[2]                               = {};
   dependencies[0].srcSubpass                                        = VK_SUBPASS_EXTERNAL;
   dependencies[0].srcStageMask                                      = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
   dependencies[0].dstStageMask                                      = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
   dependencies[0].srcAccessMask                                     = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
   dependencies[0].dstAccessMask                                     = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;;

   dependencies[1].srcSubpass                                        = VK_SUBPASS_EXTERNAL;
   dependencies[1].srcStageMask                                      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   dependencies[1].dstStageMask                                      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   dependencies[1].dstAccessMask                                     = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;;

   VkRenderPassCreateInfo render_pass_create_info                    = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
   render_pass_create_info.attachmentCount                           = 2;
   render_pass_create_info.pAttachments                              = attachments;
   render_pass_create_info.subpassCount                              = 1;
   render_pass_create_info.pSubpasses                                = &subpass_description;
   render_pass_create_info.dependencyCount                           = 2;
   render_pass_create_info.pDependencies                             = dependencies;

   vkCreateRenderPass(pRenderer->Device, &render_pass_create_info, nullptr, pRenderPass);
}

void CreateGeometryBuffers(
   VulkanRenderer*   pRenderer,
   VulkanBuffer*     pIndexBuffer,
   VulkanBuffer*     pPositionBuffer,
   VulkanBuffer*     pVertexColorBuffer)
{
   TriMesh mesh = TriMesh::Cube(vec3(1), false, {.enableVertexColors = true});

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(mesh.GetTriangles()),
      DataPtr(mesh.GetTriangles()),
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      0,
      pIndexBuffer));

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(mesh.GetPositions()),
      DataPtr(mesh.GetPositions()),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      0,
      pPositionBuffer));

   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(mesh.GetVertexColors()),
      DataPtr(mesh.GetVertexColors()),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      0,
      pVertexColorBuffer));
}

void CreateFrameBuffers()
{

}
