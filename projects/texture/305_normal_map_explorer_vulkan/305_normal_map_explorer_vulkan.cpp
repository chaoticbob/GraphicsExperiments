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

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngleX = 0.0f;
static float gAngleX       = 0.0f;
static float gTargetAngleY = 0.0f;
static float gAngleY       = 0.0f;

struct CameraProperties
{
    mat4 ModelMatrix;
    mat4 ViewProjectionMatrix;
    vec3 EyePosition;
};

struct TextureSet
{
    std::string name;
    VulkanImage diffuseTexture;
    VulkanImage normalTexture;
};

struct Geometry
{
    std::string  name;
    VulkanBuffer indexBuffer;
    uint32_t     numIndices;
    VulkanBuffer positionBuffer;
    VulkanBuffer texCoordBuffer;
    VulkanBuffer normalBuffer;
    VulkanBuffer tangentBuffer;
    VulkanBuffer bitangentBuffer;
};

void CreatePipelineLayout(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout);
void CreateTextureSets(
    VulkanRenderer*          pRenderer,
    std::vector<TextureSet>& outTextureSets);
void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvVS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleVS,
    VkShaderModule*              pModuleFS);
void CreateDescriptorBuffer(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout pDescriptorSetLayout,
    VulkanBuffer*         pBuffer);
void WriteDescriptors(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pDescriptorBuffer,
    const TextureSet&     textureSet,
    VkSampler             sampler);
void CreateGeometryBuffers(
    VulkanRenderer*        pRenderer,
    std::vector<Geometry>& outGeometries);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    int dx = x - prevX;
    int dy = y - prevY;

    if (buttons & MOUSE_BUTTON_RIGHT)
    {
        gTargetAngleX += 0.25f * dy;
    }
    if (buttons & MOUSE_BUTTON_LEFT)
    {
        gTargetAngleY += 0.25f * dx;
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
        std::string shaderSource = LoadString("projects/305_normal_map_explorer/shaders.hlsl");
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
    // Graphics pipeline state object
    // *************************************************************************
    VkPipeline pipelineState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        pipelineLayout.PipelineLayout,
        moduleVS,
        moduleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipelineState));

    // *************************************************************************
    // Texture
    // *************************************************************************
    std::vector<TextureSet> textureSets;
    CreateTextureSets(renderer.get(), textureSets);

    // *************************************************************************
    // Descriptor buffers
    // *************************************************************************
    VulkanBuffer envDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), pipelineLayout.DescriptorSetLayout, &envDescriptorBuffer);

    VkSamplerCreateInfo samplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.flags                   = 0;
    samplerInfo.magFilter               = VK_FILTER_NEAREST;
    samplerInfo.minFilter               = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias              = 0;
    samplerInfo.anisotropyEnable        = VK_FALSE;
    samplerInfo.maxAnisotropy           = 0;
    samplerInfo.compareEnable           = VK_TRUE;
    samplerInfo.compareOp               = VK_COMPARE_OP_NEVER;
    samplerInfo.minLod                  = 0;
    samplerInfo.maxLod                  = 1;
    samplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    CHECK_CALL(vkCreateSampler(
        renderer->Device,
        &samplerInfo,
        nullptr,
        &sampler));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    std::vector<Geometry> geometries;
    CreateGeometryBuffers(
        renderer.get(),
        geometries);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "305_normal_map_explorer_vulkan");
    if (!window)
    {
        assert(false && "Window::Create failed");
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
    if (!window->InitImGuiForVulkan(renderer.get(), renderPass.RenderPass))
    {
        assert(false && "Window::InitImGuiForVulkan failed");
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
    // Misc vars
    // *************************************************************************
    uint32_t textureSetIndex        = 0;
    uint32_t currentTextureSetIndex = ~0;
    uint32_t geoIndex               = 0;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color = {
        {0.0f, 0.0f, 0.2f, 1.0f}
    };
    clearValues[1].depthStencil = {1.0f, 0};

    while (window->PollEvents())
    {
        window->ImGuiNewFrameVulkan();
        if (ImGui::Begin("Scene"))
        {
            static const char* currentTextureSetName = textureSets[0].name.c_str();
            if (ImGui::BeginCombo("Textures", currentTextureSetName))
            {
                for (size_t i = 0; i < textureSets.size(); ++i)
                {
                    bool isSelected = (currentTextureSetName == textureSets[i].name);
                    if (ImGui::Selectable(textureSets[i].name.c_str(), isSelected))
                    {
                        currentTextureSetName = textureSets[i].name.c_str();
                        textureSetIndex       = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            static const char* currentGeoName = geometries[0].name.c_str();
            if (ImGui::BeginCombo("Geometry", currentGeoName))
            {
                for (size_t i = 0; i < geometries.size(); ++i)
                {
                    bool isSelected = (currentGeoName == geometries[i].name);
                    if (ImGui::Selectable(geometries[i].name.c_str(), isSelected))
                    {
                        currentGeoName = geometries[i].name.c_str();
                        geoIndex       = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::End();

        // ---------------------------------------------------------------------
        if (currentTextureSetIndex != textureSetIndex)
        {
            currentTextureSetIndex = textureSetIndex;

            auto& textureSet = textureSets[currentTextureSetIndex];

            WriteDescriptors(
                renderer.get(),
                pipelineLayout.DescriptorSetLayout,
                &envDescriptorBuffer,
                textureSet,
                sampler);
        }

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
            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                images[bufferIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_PRESENT,
                RESOURCE_STATE_RENDER_TARGET);

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

            VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
            descriptorBufferBindingInfo.pNext                            = nullptr;
            descriptorBufferBindingInfo.address                          = GetDeviceAddress(renderer.get(), &envDescriptorBuffer);
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

            VkViewport viewport = {0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {0, 0, gWindowWidth, gWindowHeight};
            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

            // Smooth out the rotation
            gAngleX += (gTargetAngleX - gAngleX) * 0.1f;
            gAngleY += (gTargetAngleY - gAngleY) * 0.1f;

            mat4 modelMat = glm::rotate(glm::radians(gAngleY), vec3(0, 1, 0)) *
                            glm::rotate(glm::radians(gAngleX), vec3(1, 0, 0));

            vec3 eyePos      = vec3(0, 1.0f, 1.25f);
            mat4 viewMat     = glm::lookAt(eyePos, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
            mat4 projViewMat = projMat * viewMat;

            CameraProperties cameraParams     = {};
            cameraParams.ModelMatrix          = modelMat;
            cameraParams.ViewProjectionMatrix = projViewMat;
            cameraParams.EyePosition          = eyePos;

            vkCmdPushConstants(cmdBuf.CommandBuffer, pipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(CameraProperties), &cameraParams);

            auto& geo = geometries[geoIndex];

            // Bind the Index Buffer
            vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, geo.indexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

            // Bind the Vertex Buffer
            VkBuffer vertexBuffers[] = {
                geo.positionBuffer.Buffer,
                geo.texCoordBuffer.Buffer,
                geo.normalBuffer.Buffer,
                geo.tangentBuffer.Buffer,
                geo.bitangentBuffer.Buffer};
            VkDeviceSize offsets[] = {0, 0, 0, 0, 0};
            vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 5, vertexBuffers, offsets);

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineState);

            vkCmdDrawIndexed(cmdBuf.CommandBuffer, geo.numIndices, 1, 0, 0, 0);

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

                VkRect2D scissor     = {0, 0, gWindowWidth, gWindowHeight};
                beginInfo.renderArea = scissor;

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

void CreatePipelineLayout(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout)
{
    // Descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {};

        // Push Constant
        // ConstantBuffer<CameraProperties> Camera              : register(b0); // Constant buffer

        // Texture2D                        DiffuseTexture      : register(t1); // Texture
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 1;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                        NormalTexture       : register(t2); // Texture
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 2;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                     Sampler0            : register(s4); // Sampler
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 4;
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

    VkPushConstantRange push_constant = {};
    push_constant.offset              = 0;
    push_constant.size                = sizeof(CameraProperties);
    push_constant.stageFlags          = VK_SHADER_STAGE_ALL_GRAPHICS;

    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.pushConstantRangeCount     = 1;
    createInfo.pPushConstantRanges        = &push_constant;
    createInfo.setLayoutCount             = 1;
    createInfo.pSetLayouts                = &pLayout->DescriptorSetLayout;

    CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, &pLayout->PipelineLayout));
}

void CreateTextureSets(
    VulkanRenderer*          pRenderer,
    std::vector<TextureSet>& outTextureSets)
{
    // Texture dir
    auto texturesDir = GetAssetPath("textures");

    // Get material files
    std::vector<std::filesystem::path> materialFiles;
    for (auto& entry : std::filesystem::directory_iterator(texturesDir))
    {
        if (!entry.is_directory())
        {
            continue;
        }
        auto materialFilePath = entry.path() / "material.mat";
        if (!fs::exists(materialFilePath))
        {
            continue;
        }
        materialFiles.push_back(materialFilePath);
    }

    size_t maxEntries = materialFiles.size();
    for (size_t i = 0; i < maxEntries; ++i)
    {
        auto materialFile = materialFiles[i];

        std::ifstream is = std::ifstream(materialFile.string().c_str());
        if (!is.is_open())
        {
            assert(false && "faild to open material file");
        }

        TextureSet textureSet = {};
        textureSet.name       = materialFile.parent_path().filename().string();

        while (!is.eof())
        {
            VulkanImage*          pTargetTexture = nullptr;
            std::filesystem::path textureFile    = "";

            std::string key;
            is >> key;
            if (key == "basecolor")
            {
                is >> textureFile;
                pTargetTexture = &textureSet.diffuseTexture;
            }
            else if (key == "normal")
            {
                is >> textureFile;
                pTargetTexture = &textureSet.normalTexture;
            }

            if (textureFile.empty())
            {
                continue;
            }

            auto cwd    = materialFile.parent_path().filename();
            textureFile = "textures" / cwd / textureFile;

            auto bitmap = LoadImage8u(textureFile);
            if (!bitmap.Empty())
            {
                MipmapRGBA8u mipmap = MipmapRGBA8u(
                    bitmap,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_FILTER_MODE_NEAREST);

                std::vector<MipOffset> mipOffsets;
                for (auto& srcOffset : mipmap.GetOffsets())
                {
                    MipOffset dstOffset = {};
                    dstOffset.Offset    = srcOffset;
                    dstOffset.RowStride = mipmap.GetRowStride();
                    mipOffsets.push_back(dstOffset);
                }

                CHECK_CALL(CreateTexture(
                    pRenderer,
                    mipmap.GetWidth(0),
                    mipmap.GetHeight(0),
                    VK_FORMAT_R8G8B8A8_UNORM,
                    mipOffsets,
                    mipmap.GetSizeInBytes(),
                    mipmap.GetPixels(),
                    pTargetTexture));

                GREX_LOG_INFO("Created texture from " << textureFile);
            }
            else
            {
                GREX_LOG_ERROR("Failed to load: " << textureFile);
                assert(false && "Failed to load texture!");
            }
        }

        outTextureSets.push_back(textureSet);
    }

    if (outTextureSets.empty())
    {
        assert(false && "No textures!");
    }
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

void WriteDescriptors(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pDescriptorBuffer,
    const TextureSet&     textureSet,
    VkSampler             sampler)
{
    char* pDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        pRenderer->Allocator,
        pDescriptorBuffer->Allocation,
        reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // Push Constant
    // ConstantBuffer<CameraProperties> Camera              : register(b0); // Constant buffer

    // Texture2D                        DiffuseTexture      : register(t1); // Texture
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            &textureSet.diffuseTexture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R8G8B8A8_UNORM,
            GREX_ALL_SUBRESOURCES,
            &imageView));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            1, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }
    // Texture2D                        NormalTexture       : register(t2); // Texture
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            &textureSet.normalTexture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R8G8B8A8_UNORM,
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

    // SamplerState                     Sampler0            : register(s4); // Sampler
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        4, // binding
        0, // arrayElement
        sampler);

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}

void CreateGeometryBuffers(
    VulkanRenderer*        pRenderer,
    std::vector<Geometry>& outGeometries)
{
    TriMesh::Options options = {.enableTexCoords = true, .enableNormals = true, .enableTangents = true};

    std::vector<TriMesh> meshes;
    // Cube
    {
        Geometry geometry = {.name = "Cube"};
        outGeometries.push_back(geometry);

        TriMesh mesh = TriMesh::Cube(vec3(1), false, options);
        meshes.push_back(mesh);
    }

    // Sphere
    {
        Geometry geometry = {.name = "Sphere"};
        outGeometries.push_back(geometry);

        TriMesh mesh = TriMesh::Sphere(0.5f, 64, 32, options);
        meshes.push_back(mesh);
    }

    // Plane
    {
        Geometry geometry = {.name = "Plane"};
        outGeometries.push_back(geometry);

        TriMesh mesh = TriMesh::Plane(vec2(1.5f), 1, 1, vec3(0, 1, 0), options);
        meshes.push_back(mesh);
    }

    // Material Knob
    {
        Geometry geometry = {.name = "Material Knob"};
        outGeometries.push_back(geometry);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh))
        {
            assert(false && "Failed to load material knob");
        }
        mesh.ScaleToFit(0.75f);
        meshes.push_back(mesh);
    }

    // Monkey
    {
        Geometry geometry = {.name = "Monkey"};
        outGeometries.push_back(geometry);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/monkey.obj").string(), "", options, &mesh))
        {
            assert(false && "Failed to load material knob");
        }
        mesh.ScaleToFit(0.75f);
        meshes.push_back(mesh);
    }

    for (size_t i = 0; i < meshes.size(); ++i)
    {
        auto& mesh     = meshes[i];
        auto& geometry = outGeometries[i];

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &geometry.indexBuffer));

        geometry.numIndices = mesh.GetNumIndices();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &geometry.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &geometry.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &geometry.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &geometry.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &geometry.bitangentBuffer));
    }
}