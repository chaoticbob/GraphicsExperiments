#include "window.h"

#include "vk_renderer.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define CHECK_CALL(FN)                                                 \
    {                                                                  \
        VkResult vkres = FN;                                           \
        if (vkres != VK_SUCCESS)                                       \
        {                                                              \
            std::stringstream ss;                                      \
            ss << "\n";                                                \
            ss << "*** FUNCTION CALL FAILED *** \n";                   \
            ss << "LOCATION: " << __FILE__ << ":" << __LINE__ << "\n"; \
            ss << "FUNCTION: " << #FN << "\n";                         \
            ss << "\n";                                                \
            GREX_LOG_ERROR(ss.str().c_str());                          \
            assert(false);                                             \
        }                                                              \
    }

// =============================================================================
// Shader code
// =============================================================================

const char* gShaderRGEN = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0) uniform CameraProperties 
{
	mat4 viewInverse;
	mat4 projInverse;
} cam;


void main() 
{
	const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 inUV = pixelCenter/vec2(gl_LaunchSizeEXT.xy);

	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(inUV, 0, 0));
}

)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth        = 1280;
static uint32_t gWindowHeight       = 720;
static bool     gEnableDebug        = true;
static uint32_t gUniformmBufferSize = 256;

void CreateDescriptorSetLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout* pLayout);
void CreatePipelineLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout* pLayout);
void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvRGEN,
    VkShaderModule*              pModuleRGEN);
void CreateRayTracingPipeline(
    VulkanRenderer*  pRenderer,
    VkShaderModule   moduleRGEN,
    VkPipelineLayout pipelineLayout,
    VkPipeline*      pPipeline);
void CreateShaderBindingTables(
    VulkanRenderer*                                  pRenderer,
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
    VkPipeline                                       pipeline,
    VulkanBuffer*                                    pRayGenSBT);
void CreateUniformBuffer(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer);
void CreateDescriptors(
    VulkanRenderer*      pRenderer,
    VulkanDescriptorSet* pDescriptors,
    VkImageView          pBackBuffer,
    VulkanBuffer*        pCameraBuffer);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    VulkanFeatures features   = {};
    features.EnableRayTracing = true;
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
    std::vector<uint32_t> spirvRGEN;
    {
        std::string   errorMsg;
        CompileResult res = CompileGLSL(gShaderRGEN, VK_SHADER_STAGE_RAYGEN_BIT_KHR, {}, &spirvRGEN, &errorMsg);
        if (res != COMPILE_SUCCESS)
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (RGEN): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Descriptor Set Layout
    // *************************************************************************
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    CreateDescriptorSetLayout(renderer.get(), &descriptorSetLayout);

    // *************************************************************************
    // Pipeline layout
    //
    // This is used for pipeline creation and setting the descriptor buffer(s).
    //
    // *************************************************************************
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    CreatePipelineLayout(renderer.get(), descriptorSetLayout, &pipelineLayout);

    // *************************************************************************
    // Shader module
    // *************************************************************************
    VkShaderModule moduleRGEN = VK_NULL_HANDLE;
    VkShaderModule moduleMISS = VK_NULL_HANDLE;
    VkShaderModule moduleCHIT = VK_NULL_HANDLE;
    CreateShaderModules(
        renderer.get(),
        spirvRGEN,
        &moduleRGEN);

    // *************************************************************************
    // Get ray tracing properties
    // *************************************************************************
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    {
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &rayTracingProperties;
        vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
    }

    // *************************************************************************
    // Ray tracing pipeline
    //
    // The pipeline is created with 1 shader groups:
    //    1) Ray gen
    //
    // *************************************************************************
    VkPipeline pipeline = VK_NULL_HANDLE;
    CreateRayTracingPipeline(
        renderer.get(),
        moduleRGEN,
        pipelineLayout,
        &pipeline);

    // *************************************************************************
    // Shader binding tables
    //
    // This assumes that there are 1 shader groups in the pipeline:
    //    1) Ray gen
    //
    // *************************************************************************
    VulkanBuffer rgenSBT = {};
    CreateShaderBindingTables(
        renderer.get(),
        rayTracingProperties,
        pipeline,
        &rgenSBT);

    // *************************************************************************
    // Uniform buffer
    // *************************************************************************
    VulkanBuffer uniformBuffer = {};
    CreateUniformBuffer(renderer.get(), &uniformBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, GREX_BASE_FILE_NAME());
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    auto surface = window->CreateVkSurface(renderer->Instance);
    if (!surface)
    {
        assert(false && "CreateVkSurface failed");
        return EXIT_FAILURE;
    }

    if (!InitSwapchain(renderer.get(), surface, window->GetWidth(), window->GetHeight()))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain image views
    // *************************************************************************
    std::vector<VkImage>             images;
    std::vector<VkImageView>         imageViews;
    std::vector<VulkanDescriptorSet> descriptors;
    {
        CHECK_CALL(GetSwapchainImages(renderer.get(), images));

        for (auto& image : images)
        {
            VkImageViewCreateInfo createInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image                           = image;
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = VK_FORMAT_B8G8R8A8_UNORM;
            createInfo.components                      = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            CHECK_CALL(vkCreateImageView(renderer->Device, &createInfo, nullptr, &imageView));

            imageViews.push_back(imageView);
            descriptors.push_back(VulkanDescriptorSet());
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
    while (window->PollEvents())
    {
        uint32_t imageIndex = 0;
        if (AcquireNextImage(renderer.get(), &imageIndex))
        {
            assert(false && "AcquireNextImage failed");
            break;
        }

        CreateDescriptors(
            renderer.get(),
            &descriptors[imageIndex],
            imageViews[imageIndex],
            &uniformBuffer);

        // Build command buffer to trace rays
        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));
        {
            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                images[imageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_PRESENT,
                RESOURCE_STATE_COMMON);

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);

            vkCmdBindDescriptorSets(
                cmdBuf.CommandBuffer,
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                pipelineLayout,
                0, // firstSet
                1, // setCount
                &descriptors[imageIndex].DescriptorSet,
                0,
                nullptr);

            const uint32_t alignedHandleSize = Align(
                rayTracingProperties.shaderGroupHandleSize,
                rayTracingProperties.shaderGroupHandleAlignment);

            VkStridedDeviceAddressRegionKHR rgenShaderSBTEntry = {};
            rgenShaderSBTEntry.deviceAddress                   = GetDeviceAddress(renderer.get(), &rgenSBT);
            rgenShaderSBTEntry.stride                          = alignedHandleSize;
            rgenShaderSBTEntry.size                            = alignedHandleSize;

            VkStridedDeviceAddressRegionKHR missShaderSBTEntry = {};

            VkStridedDeviceAddressRegionKHR chitShaderSBTEntry = {};

            VkStridedDeviceAddressRegionKHR callableShaderSbtEntry = {};

            fn_vkCmdTraceRaysKHR(
                cmdBuf.CommandBuffer,
                &rgenShaderSBTEntry,
                &missShaderSBTEntry,
                &chitShaderSBTEntry,
                &callableShaderSbtEntry,
                gWindowWidth,
                gWindowHeight,
                1);

            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                images[imageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_COMMON,
                RESOURCE_STATE_PRESENT);
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

void CreateDescriptorSetLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout* pLayout)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    // layout(binding = 1, set = 0, rgba8) uniform image2D image;
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 1;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings.push_back(binding);
    }
    // layout(binding = 2, set = 0) uniform CameraProperties
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 2;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.bindingCount                    = CountU32(bindings);
    createInfo.pBindings                       = DataPtr(bindings);

    CHECK_CALL(vkCreateDescriptorSetLayout(pRenderer->Device, &createInfo, nullptr, pLayout));
}

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout* pLayout)
{
    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.setLayoutCount             = 1;
    createInfo.pSetLayouts                = &descriptorSetLayout;

    CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, pLayout));
}

void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvRGEN,
    VkShaderModule*              pModuleRGEN)
{
    // Ray gen
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvRGEN);
        createInfo.pCode                    = DataPtr(spirvRGEN);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleRGEN));
    }
}

void CreateRayTracingPipeline(
    VulkanRenderer*  pRenderer,
    VkShaderModule   moduleRGEN,
    VkPipelineLayout pipelineLayout,
    VkPipeline*      pPipeline)
{
    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {};
    // Ray gen
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        createInfo.module                          = moduleRGEN;
        createInfo.pName                           = "main";

        shaderStages.push_back(createInfo);
    }

    // Shader groups
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups = {};
    // Ray gen
    {
        VkRayTracingShaderGroupCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        createInfo.generalShader                        = 0; // shaderStages[0]
        createInfo.closestHitShader                     = VK_SHADER_UNUSED_KHR;
        createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
        createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

        shaderGroups.push_back(createInfo);
    }

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.stageCount                        = CountU32(shaderStages);
    createInfo.pStages                           = DataPtr(shaderStages);
    createInfo.groupCount                        = CountU32(shaderGroups);
    createInfo.pGroups                           = DataPtr(shaderGroups);
    createInfo.maxPipelineRayRecursionDepth      = 1;
    createInfo.layout                            = pipelineLayout;
    createInfo.basePipelineHandle                = VK_NULL_HANDLE;
    createInfo.basePipelineIndex                 = -1;

    CHECK_CALL(fn_vkCreateRayTracingPipelinesKHR(
        pRenderer->Device, // device
        VK_NULL_HANDLE,    // deferredOperation
        VK_NULL_HANDLE,    // pipelineCache
        1,                 // createInfoCount
        &createInfo,       // pCreateInfos
        nullptr,           // pAllocator
        pPipeline));       // pPipelines
}

void CreateShaderBindingTables(
    VulkanRenderer*                                  pRenderer,
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
    VkPipeline                                       pipeline,
    VulkanBuffer*                                    pRayGenSBT)
{
    // Hardcoded group count
    const uint32_t groupCount = 1;

    // Handle sizes
    uint32_t handleSize                 = rayTracingProperties.shaderGroupHandleSize;
    uint32_t shaderGroupHandleAlignment = rayTracingProperties.shaderGroupHandleAlignment;
    uint32_t alignedHandleSize          = Align(handleSize, shaderGroupHandleAlignment);
    uint32_t handesDataSize             = groupCount * alignedHandleSize;

    //
    // This is what the shader group handles look like
    // in handlesData based on the pipeline. The offsets
    // are in bytes.
    //
    //  +--------+
    //  |  RGEN  | offset = 0
    //  +--------+
    //
    std::vector<char> handlesData(handesDataSize);
    CHECK_CALL(fn_vkGetRayTracingShaderGroupHandlesKHR(
        pRenderer->Device,    // device
        pipeline,             // pipeline
        0,                    // firstGroup
        groupCount,           // groupCount
        handesDataSize,       // dataSize
        handlesData.data())); // pData)

    // Usage flags for SBT buffer
    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;

    char* pShaderGroupHandleRGEN = handlesData.data();

    //
    // Create buffers for each shader group's SBT and copy the
    // the shader group handles into each buffer.
    //
    // The size of the SBT buffers must be aligned to
    // VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupBaseAlignment.
    //
    const uint32_t shaderGroupBaseAlignment = rayTracingProperties.shaderGroupBaseAlignment;
    // Ray gen
    {
        CHECK_CALL(CreateBuffer(
            pRenderer,                // pRenderer
            handleSize,               // srcSize
            pShaderGroupHandleRGEN,   // pSrcData
            usageFlags,               // usageFlags
            shaderGroupBaseAlignment, // minAlignment
            pRayGenSBT));             // pBuffer
    }
}

void CreateUniformBuffer(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer)
{
    struct Camera
    {
        glm::mat4 viewInverse;
        glm::mat4 projInverse;
    };

    Camera camera      = {};
    camera.projInverse = glm::inverse(glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 512.0f));
    camera.viewInverse = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, -2.5f)));

    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    CHECK_CALL(CreateBuffer(
        pRenderer,           // pRenderer
        gUniformmBufferSize, // srcSize
        &camera,             // pSrcData
        usageFlags,          // usageFlags
        256,                 // minAlignment
        pBuffer));           // pBuffer
}

void CreateDescriptors(
    VulkanRenderer*      pRenderer,
    VulkanDescriptorSet* pDescriptors,
    VkImageView          pBackBuffer,
    VulkanBuffer*        pCameraBuffer)
{
    // Most Vulkan implementations support STORAGE_IMAGE so we can write directly to the image and skip a copy.
    // layout(binding = 1, set = 0, rgba8) uniform image2D image;
    VulkanImageDescriptor backbufferDescriptor;
    CreateDescriptor(
        pRenderer,
        &backbufferDescriptor,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        1, // binding,
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        pBackBuffer,
        VK_IMAGE_LAYOUT_GENERAL);

    // layout(binding = 2, set = 0) uniform CameraProperties
    VulkanBufferDescriptor cameraPropertiesDescriptor;
    CreateDescriptor(
        pRenderer,
        &cameraPropertiesDescriptor,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        2, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        pCameraBuffer);

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings =
        {
            backbufferDescriptor.layoutBinding,
            cameraPropertiesDescriptor.layoutBinding,
        };

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
        {
            backbufferDescriptor.writeDescriptorSet,
            cameraPropertiesDescriptor.writeDescriptorSet,
        };

    DestroyDescriptorSet(pRenderer, pDescriptors);
    CreateAndUpdateDescriptorSet(pRenderer, layoutBindings, writeDescriptorSets, pDescriptors);
}
