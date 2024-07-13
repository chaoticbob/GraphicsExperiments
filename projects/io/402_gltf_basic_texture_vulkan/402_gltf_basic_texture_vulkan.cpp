#include "window.h"

#include "vk_renderer.h"

#include "vk_faux_render.h"

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

#define MAX_INSTANCES         100
#define MAX_MATERIALS         100
#define MAX_MATERIAL_SAMPLERS 32
#define MAX_MATERIAL_IMAGES   1024
#define MAX_IBL_TEXTURES      1

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
static uint32_t gWindowWidth      = 1280;
static uint32_t gWindowHeight     = 720;
static bool     gEnableDebug      = true;

static std::vector<std::string> gIBLNames = {};

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvVS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleVS,
    VkShaderModule*              pModuleFS);
void CreatePipelineLayout(
    VulkanRenderer*       pRenderer,
    VulkanPipelineLayout* pLayout);

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
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    VulkanFeatures features = {};
    if (!InitVulkan(renderer.get(), gEnableDebug, features))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<uint32_t> spirvVS;
    std::vector<uint32_t> spirvFS;
    {
        std::string shaderSource = LoadString("faux_render_shaders/render_base_color.hlsl");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &spirvVS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &spirvFS, &errorMsg);
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
    // Pipeline layout
    //
    // This is used for pipeline creation
    //
    // *************************************************************************
    VulkanPipelineLayout pipelineLayout = {};
    CreatePipelineLayout(renderer.get(), &pipelineLayout);

    // *************************************************************************
    // Scene
    // *************************************************************************
    VkFauxRender::SceneGraph graph = VkFauxRender::SceneGraph(renderer.get(), &pipelineLayout);
    if (!FauxRender::LoadGLTF(GetAssetPath("scenes/basic_texture.gltf"), {}, &graph))
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
    // Graphics pipeline state object
    // *************************************************************************
    VkPipeline pipelineState;
    CHECK_CALL(CreateGraphicsPipeline2(
        renderer.get(),
        pipelineLayout.PipelineLayout,
        moduleVS,
        moduleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipelineState));

    // *************************************************************************
    // Descriptors
    // *************************************************************************
    {
        void*         pDescriptorBufferStartAddress = nullptr;
        VulkanBuffer* descriptorBuffer              = &static_cast<VkFauxRender::SceneGraph*>(&graph)->DescriptorBuffer;

        vmaMapMemory(renderer->Allocator, descriptorBuffer->Allocation, &pDescriptorBufferStartAddress);

        // Material Textures
        {
            for (size_t i = 0; i < graph.Images.size(); ++i)
            {
                auto               image    = VkFauxRender::Cast(graph.Images[i].get());
                const VulkanImage* resource = &image->Resource;

                VkImageView imageView = VK_NULL_HANDLE;
                CHECK_CALL(CreateImageView(
                    renderer.get(),
                    resource,
                    VK_IMAGE_VIEW_TYPE_2D,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    GREX_ALL_SUBRESOURCES,
                    &imageView));

                WriteDescriptor(
                    renderer.get(),
                    pDescriptorBufferStartAddress,
                    pipelineLayout.DescriptorSetLayout,
                    MATERIAL_IMAGES_START_REGISTER,
                    static_cast<uint32_t>(i),
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    imageView,
                    VK_IMAGE_LAYOUT_GENERAL);
            }
        }

        // Material Samplers
        {
            // Clamped
            {
                VkSamplerCreateInfo clampedSamplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                clampedSamplerInfo.flags                   = 0;
                clampedSamplerInfo.magFilter               = VK_FILTER_LINEAR;
                clampedSamplerInfo.minFilter               = VK_FILTER_LINEAR;
                clampedSamplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                clampedSamplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                clampedSamplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                clampedSamplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                clampedSamplerInfo.mipLodBias              = 0;
                clampedSamplerInfo.anisotropyEnable        = VK_FALSE;
                clampedSamplerInfo.maxAnisotropy           = 0;
                clampedSamplerInfo.compareEnable           = VK_TRUE;
                clampedSamplerInfo.compareOp               = VK_COMPARE_OP_LESS_OR_EQUAL;
                clampedSamplerInfo.minLod                  = 0;
                clampedSamplerInfo.maxLod                  = 1;
                clampedSamplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                clampedSamplerInfo.unnormalizedCoordinates = VK_FALSE;

                VkSampler clampedSampler = VK_NULL_HANDLE;
                CHECK_CALL(vkCreateSampler(
                    renderer.get()->Device,
                    &clampedSamplerInfo,
                    nullptr,
                    &clampedSampler));

                WriteDescriptor(
                    renderer.get(),
                    pDescriptorBufferStartAddress,
                    pipelineLayout.DescriptorSetLayout,
                    MATERIAL_SAMPLER_START_REGISTER, // binding
                    0,                               // arrayElement
                    clampedSampler);
            }

            // Repeat
            {
                VkSamplerCreateInfo repeatSamplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                repeatSamplerInfo.flags                   = 0;
                repeatSamplerInfo.magFilter               = VK_FILTER_LINEAR;
                repeatSamplerInfo.minFilter               = VK_FILTER_LINEAR;
                repeatSamplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                repeatSamplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                repeatSamplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                repeatSamplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                repeatSamplerInfo.mipLodBias              = 0;
                repeatSamplerInfo.anisotropyEnable        = VK_FALSE;
                repeatSamplerInfo.maxAnisotropy           = 0;
                repeatSamplerInfo.compareEnable           = VK_TRUE;
                repeatSamplerInfo.compareOp               = VK_COMPARE_OP_LESS_OR_EQUAL;
                repeatSamplerInfo.minLod                  = 0;
                repeatSamplerInfo.maxLod                  = 1;
                repeatSamplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                repeatSamplerInfo.unnormalizedCoordinates = VK_FALSE;

                VkSampler repeatSampler = VK_NULL_HANDLE;
                CHECK_CALL(vkCreateSampler(
                    renderer.get()->Device,
                    &repeatSamplerInfo,
                    nullptr,
                    &repeatSampler));

                WriteDescriptor(
                    renderer.get(),
                    pDescriptorBufferStartAddress,
                    pipelineLayout.DescriptorSetLayout,
                    MATERIAL_SAMPLER_START_REGISTER, // binding
                    1,                               // arrayElement
                    repeatSampler);
            }
        }

        vmaUnmapMemory(renderer->Allocator, descriptorBuffer->Allocation);
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "402_gltf_basic_texture_vulkan");
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

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
    std::vector<VkImage>     images;
    std::vector<VkImageView> imageViews;
    std::vector<VkImageView> depthViews;
    {
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
    clearValues[0].color = {
        {0.23f, 0.23f, 0.31f, 0}
    };
    clearValues[1].depthStencil = {1.0f, 0};

    while (window->PollEvents())
    {
        UINT bufferIndex = 0;
        if (AcquireNextImage(renderer.get(), &bufferIndex))
        {
            assert(false && "AcquireNextImage failed");
            break;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        {
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

            VkRenderingInfo vkri          = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            vkri.layerCount               = 1;
            vkri.colorAttachmentCount     = 1;
            vkri.pColorAttachments        = &colorAttachment;
            vkri.pDepthAttachment         = &depthAttachment;
            vkri.renderArea.extent.width  = gWindowWidth;
            vkri.renderArea.extent.height = gWindowHeight;

            vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);

            // Viewport and scissor
            VkViewport viewport = {0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {0, 0, gWindowWidth, gWindowHeight};
            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineState);

            // Draw scene
            const auto& scene = graph.Scenes[0];
            VkFauxRender::Draw(&graph, scene.get(), &cmdBuf);
        }

        vkCmdEndRendering(cmdBuf.CommandBuffer);

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        // Execute command buffer
        CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

        // Wait for the GPU to finish the work
        if (!WaitForGpu(renderer.get()))
        {
            assert(false && "WaitForGpu failed");
            break;
        }

        // Present
        if (!SwapchainPresent(renderer.get(), bufferIndex))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvVS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleVS,
    VkShaderModule*              pModuleFS)
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

void CreatePipelineLayout(
    VulkanRenderer*       pRenderer,
    VulkanPipelineLayout* pLayout)
{
    // Descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {};

        // ConstantBuffer<SceneData>      Scene                                   : register(SCENE_REGISTER);                     // Scene constants
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = SCENE_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }

        // ConstantBuffer<CameraData>     Camera                                  : register(CAMERA_REGISTER);                     // Camera constants
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = CAMERA_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }

        // DEFINE_AS_PUSH_CONSTANT
        // ConstantBuffer<DrawData>       Draw                                    : register(DRAW_REGISTER);                       // Draw root constants

        // StructuredBuffer<InstanceData> Instances                               : register(INSTANCE_BUFFER_REGISTER);            // Instance data
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = INSTANCE_BUFFER_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // StructuredBuffer<MaterialData> Materials                               : register(MATERIAL_BUFFER_REGISTER);            // Material data
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = MATERIAL_BUFFER_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                   MaterialSamplers[MAX_MATERIAL_SAMPLERS] : register(MATERIAL_SAMPLER_START_REGISTER);     // Material samplers
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = MATERIAL_SAMPLER_START_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = MAX_MATERIAL_SAMPLERS;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                      MaterialImages[MAX_MATERIAL_IMAGES]     : register(MATERIAL_IMAGES_START_REGISTER);      // Material images (textures)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = MATERIAL_IMAGES_START_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = MAX_MATERIAL_IMAGES;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                      IBLEnvMapTexture[MAX_IBL_TEXTURES]      : register(IBL_ENV_MAP_TEXTURE_START_REGISTER);  // IBL environment map texture
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = IBL_ENV_MAP_TEXTURE_START_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = MAX_IBL_TEXTURES;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                      IBLIrrMapTexture[MAX_IBL_TEXTURES]      : register(IBL_IRR_MAP_TEXTURE_START_REGISTER);  // IBL irradiance map texture
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = IBL_IRR_MAP_TEXTURE_START_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = MAX_IBL_TEXTURES;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                      IBLIntegrationLUT                       : register(IBL_INTEGRATION_LUT_REGISTER);        // IBL integration LUT
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = IBL_INTEGRATION_LUT_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                   IBLMapSampler                           : register(IBL_MAP_SAMPLER_REGISTER);            // IBL environment/irradiance map sampler
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = IBL_MAP_SAMPLER_REGISTER;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                   IBLIntegrationSampler                   : register(IBL_INTEGRATION_SAMPLER_REGISTER);    // IBL integration sampler
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = IBL_INTEGRATION_SAMPLER_REGISTER;
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

    // DEFINE_AS_PUSH_CONSTANT
    // ConstantBuffer<DrawData>       Draw                                    : register(DRAW_REGISTER);                       // Draw root constants
    VkPushConstantRange push_constant = {};
    push_constant.offset              = 0;
    push_constant.size                = sizeof(FauxRender::Shader::DrawParams);
    push_constant.stageFlags          = VK_SHADER_STAGE_ALL_GRAPHICS;

    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.pushConstantRangeCount     = 1;
    createInfo.pPushConstantRanges        = &push_constant;
    createInfo.setLayoutCount             = 1;
    createInfo.pSetLayouts                = &pLayout->DescriptorSetLayout;

    CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, &pLayout->PipelineLayout));
}
