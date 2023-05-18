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

struct Camera
{
   mat4 mvp;
};

// =============================================================================
// Shader code
// =============================================================================
const char* gShadersVS = R"(
#version 460

layout( push_constant ) uniform CameraProperties 
{
   mat4 MVP;
} Cam;

in vec3 PositionOS;
in vec3 Color;

out vec4 PositionCS;
out vec3 outColor;

void main()
{
    PositionCS = Cam.MVP * vec4(PositionOS, 1);
    outColor = Color;
}
)";
const char* gShadersFS = R"(
#version 460

in vec3 Color;

out vec4 FragColor;

void main()
{
    FragColor = vec4(Color, 1);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;
static bool     gEnableRaytracing = false;

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkPipelineLayout* pLayout);
void CreateShaderModules(
   VulkanRenderer*               pRenderer,
   const std::vector<uint32_t>&  spirvVS,
   const std::vector<uint32_t>&  spirvFS,
   VkShaderModule*               pModuleVS,
   VkShaderModule*               pModuleFS);
void CreateGeometryBuffers(
    VulkanRenderer*  pRenderer,
    VulkanBuffer*    pIndexBuffer,
    VulkanBuffer*    pVertexBuffer,
    VulkanBuffer*    pVertexColorBuffer,
    uint32_t*        pNumIndices,
    VulkanBuffer*    pTBNVertexBuffer,
    uint32_t*        pNumTBNVertices);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    if (!InitVulkan(renderer.get(), gEnableDebug, gEnableRaytracing)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<uint32_t> spirvVS;
    std::vector<uint32_t> spirvFS;
    {
        std::string errorMsg;
        CompileResult vkRes = CompileGLSL(gShadersVS, "main", VK_SHADER_STAGE_VERTEX_BIT, {}, &spirvVS, &errorMsg);
        if (vkRes != COMPILE_SUCCESS) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        vkRes = CompileGLSL(gShadersFS, "main", VK_SHADER_STAGE_FRAGMENT_BIT, {}, &spirvFS, &errorMsg);
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
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
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
    // Create the pipeline
    //
    // The pipeline is created with 2 shaders
    //    1) Vertex Shader
    //    2) Fragment Shader
    //
    // *************************************************************************
    VkPipeline trianglePipelineState = VK_NULL_HANDLE;
    CHECK_CALL(CreateDrawVertexColorPipeline(
       renderer.get(),
       pipelineLayout,
       moduleVS,
       moduleFS,
       GREX_DEFAULT_RTV_FORMAT,
       GREX_DEFAULT_DSV_FORMAT,
       &trianglePipelineState));

    VkPipeline tbnDebugPipelineState;
    CHECK_CALL(CreateDrawVertexColorPipeline(
       renderer.get(),
       pipelineLayout,
       moduleVS,
       moduleFS,
       GREX_DEFAULT_RTV_FORMAT,
       GREX_DEFAULT_DSV_FORMAT,
       &tbnDebugPipelineState,
       VK_CULL_MODE_NONE,
       VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
       VK_PIPELINE_FLAGS_INTERLEAVED_ATTRS));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    VulkanBuffer indexBuffer;
    VulkanBuffer positionBuffer;
    VulkanBuffer vertexColorBuffer;
    uint32_t     numIndices = 0;
    VulkanBuffer tbnDebugVertexBuffer;
    uint32_t     tbnDebugNumVertices = 0;
    CreateGeometryBuffers(
        renderer.get(),
        &indexBuffer,
        &positionBuffer,
        &vertexColorBuffer,
        &numIndices,
        &tbnDebugVertexBuffer,
        &tbnDebugNumVertices);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "106_cone_vulkan");
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
    std::vector<VkImage>      images;
    std::vector<VkImageView>  imageViews;
    std::vector<VkImageView>  depthViews;
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
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color = { {0.0f, 0.0f, 0.2f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    while (window->PollEvents()) {
        UINT bufferIndex = 0;
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
           vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipelineState);

           // Bind the Index Buffer
           vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, indexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

           // Bind the Vertex Buffer
           VkBuffer vertexBuffers[] = { positionBuffer.Buffer, vertexColorBuffer.Buffer };
           VkDeviceSize offsets[] = { 0, 0 };
           vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 2, vertexBuffers, offsets);

           float t = static_cast<float>(glfwGetTime());
           mat4 modelMat = glm::rotate(t, vec3(1, 0, 0));
           mat4 viewMat = glm::lookAt(vec3(0, 1, 2), vec3(0, 0, 0), vec3(0, 1, 0));
           mat4 projMat = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
           mat4 mvpMat  = projMat * viewMat * modelMat;

           vkCmdPushConstants(cmdBuf.CommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &mvpMat);

           vkCmdDraw(cmdBuf.CommandBuffer, numIndices, 1, 0, 0);

           // TBN debug
           {
              vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tbnDebugPipelineState);

              VkBuffer vertexBuffers[] = { tbnDebugVertexBuffer.Buffer };
              VkDeviceSize offsets[] = { 0 };
              vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 1, vertexBuffers, offsets);

              vkCmdDraw(cmdBuf.CommandBuffer, tbnDebugNumVertices, 1, 0, 0);
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

void CreatePipelineLayout(
   VulkanRenderer*   pRenderer, 
   VkPipelineLayout* pLayout)
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
    VulkanRenderer*  pRenderer,
    VulkanBuffer*    pIndexBuffer,
    VulkanBuffer*    pPositionBuffer,
    VulkanBuffer*    pVertexColorBuffer,
    uint32_t*        pNumIndices,
    VulkanBuffer*    pTBNVertexBuffer,
    uint32_t*        pNumTBNVertices)
{
   TriMesh mesh = TriMesh::Cone(1, 1, 32, { .enableVertexColors = true, .enableNormals = true, .enableTangents = true });

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

   *pNumIndices = 3 * mesh.GetNumTriangles();

   auto tbnVertexData = mesh.GetTBNLineSegments(pNumTBNVertices);
   CHECK_CALL(CreateBuffer(
      pRenderer,
      SizeInBytes(tbnVertexData),
      DataPtr(tbnVertexData),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      0,
      pTBNVertexBuffer));
}