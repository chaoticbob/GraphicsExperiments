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
        if (vkres != VK_SUCCESS)                     \
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

// =============================================================================
// Shader code
// =============================================================================
const char* gShaders = R"(
struct MeshOutput {
    float4 Position : SV_POSITION;
    float3 Color    : COLOR;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void msmain(out indices uint3 triangles[1], out vertices MeshOutput vertices[3]) {
    SetMeshOutputCounts(3, 1);
    triangles[0] = uint3(0, 1, 2);

    vertices[0].Position = float4(-0.5, 0.5, 0.0, 1.0);
    vertices[0].Color = float3(1.0, 0.0, 0.0);

    vertices[1].Position = float4(0.5, 0.5, 0.0, 1.0);
    vertices[1].Color = float3(0.0, 1.0, 0.0);

    vertices[2].Position = float4(0.0, -0.5, 0.0, 1.0);
    vertices[2].Color = float3(0.0, 0.0, 1.0);
}

float4 psmain(MeshOutput input) : SV_TARGET
{
    return float4(input.Color, 1);
}

)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkPipelineLayout* pLayout);
void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvMS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleMS,
    VkShaderModule*              pModuleFS);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    VulkanFeatures features   = {};
    features.EnableMeshShader = true;
    if (!InitVulkan(renderer.get(), gEnableDebug, features))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    //
    // Make sure the shaders compile before we do anything.
    //
    // *************************************************************************
    std::vector<uint32_t> spirvMS;
    std::vector<uint32_t> spirvFS;
    {
        std::string errorMsg;
        auto        hr = CompileHLSL(gShaders, "msmain", "ms_6_5", &spirvMS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (MS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(gShaders, "psmain", "ps_6_5", &spirvFS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (FS): " << errorMsg << "\n";
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
    // Shader module
    // *************************************************************************
    VkShaderModule moduleMS = VK_NULL_HANDLE;
    VkShaderModule moduleFS = VK_NULL_HANDLE;
    CreateShaderModules(
        renderer.get(),
        spirvMS,
        spirvFS,
        &moduleMS,
        &moduleFS);

    // *************************************************************************
    // Create the pipeline
    //
    // The pipeline is created with 2 shaders
    //    1) Mesh Shader
    //    2) Fragment Shader
    //
    // *************************************************************************
    VkPipeline pipeline = VK_NULL_HANDLE;
    CreateMeshShaderPipeline(
        renderer.get(),
        pipelineLayout,
        moduleMS,
        moduleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipeline,
        VK_CULL_MODE_NONE);

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
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, GREX_BASE_FILE_NAME());
    if (!window)
    {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight()))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain image views, depth buffers/views
    // *************************************************************************
    std::vector<VkImageView> imageViews;
    std::vector<VkImageView> depthViews;
    {
        std::vector<VkImage> images;
        CHECK_CALL(GetSwapchainImages(renderer.get(), images));

        for (auto& image : images)
        {
            // Create swap chain images
            VkImageViewCreateInfo createInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image                           = image;
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = GREX_DEFAULT_RTV_FORMAT;
            createInfo.components                      = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
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

        for (int depthIndex = 0; depthIndex < imageCount; depthIndex++)
        {
            // Create depth images
            CHECK_CALL(CreateDSV(renderer.get(), window->GetWidth(), window->GetHeight(), &depthImages[depthIndex]));

            VkImageViewCreateInfo createInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image                           = depthImages[depthIndex].Image;
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = GREX_DEFAULT_DSV_FORMAT;
            createInfo.components                      = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
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
    clearValues[0].color        = {0.23f, 0.23f, 0.31f, 0};
    clearValues[1].depthStencil = {1.0f, 0};

    while (window->PollEvents())
    {
        uint32_t imageIndex = 0;
        if (AcquireNextImage(renderer.get(), &imageIndex))
        {
            assert(false && "AcquireNextImage failed");
            break;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        {
            VkRenderingAttachmentInfo colorAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView                 = imageViews[imageIndex];
            colorAttachment.imageLayout               = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue                = clearValues[0];

            VkRenderingAttachmentInfo depthAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView                 = depthViews[imageIndex];
            depthAttachment.imageLayout               = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue                = clearValues[1];

            VkRenderingInfo vkri          = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            vkri.layerCount               = 1;
            vkri.colorAttachmentCount     = 1;
            vkri.pColorAttachments        = &colorAttachment;
            vkri.pDepthAttachment         = &depthAttachment;
            vkri.renderArea.extent.width  = gWindowWidth;
            vkri.renderArea.extent.height = gWindowHeight;

            vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);

            VkViewport viewport = {0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {0, 0, gWindowWidth, gWindowHeight};
            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            fn_vkCmdDrawMeshTasksEXT(cmdBuf.CommandBuffer, 1, 1, 1);

            vkCmdEndRendering(cmdBuf.CommandBuffer);
        }

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        // Execute command buffer
        CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

        // Wait for the GPU to finish the work
        if (!WaitForGpu(renderer.get()))
        {
            assert(false && "WaitForGpu failed");
        }

        if (!SwapchainPresent(renderer.get(), imageIndex))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkPipelineLayout* pLayout)
{
    VkPushConstantRange push_constant = {};
    push_constant.offset              = 0;
    push_constant.size                = sizeof(mat4);
    push_constant.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.pushConstantRangeCount     = 1;
    createInfo.pPushConstantRanges        = &push_constant;

    CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, pLayout));
}

void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvMS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleVS,
    VkShaderModule*              pModuleFS)
{
    // Mesh Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvMS);
        createInfo.pCode                    = DataPtr(spirvMS);

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
