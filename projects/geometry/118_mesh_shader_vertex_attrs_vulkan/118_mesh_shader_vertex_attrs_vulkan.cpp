#include "window.h"
#include "camera.h"

#include "vk_renderer.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include "meshoptimizer.h"

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
// Scene Stuff
// =============================================================================
struct SceneProperties
{
    mat4 InstanceM;
    mat4 CameraVP;
    vec3 EyePosition;
    uint DrawFunc;
    vec3 LightPosition;
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

enum DrawFunc
{
    DRAW_FUNC_POSITION  = 0,
    DRAW_FUNC_TEX_COORD = 1,
    DRAW_FUNC_NORMAL    = 2,
    DRAW_FUNC_PHONG     = 3,
};

static std::vector<std::string> gDrawFuncNames = {
    "Position",
    "Tex Coord",
    "Normal",
    "Phong",
};

static int gDrawFunc = DRAW_FUNC_PHONG;

void CreatePipelineLayout(
    VulkanRenderer*        pRenderer,
    VkPipelineLayout*      pPipelineLayout,
    VkDescriptorSetLayout* pDescriptorSetLayout);
void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvAS,
    const std::vector<uint32_t>& spirvMS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleAS,
    VkShaderModule*              pModuleMS,
    VkShaderModule*              pModuleFS);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    VulkanFeatures features       = {};
    features.EnableMeshShader     = true;
    features.EnablePushDescriptor = true;
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
    std::vector<uint32_t> spirvAS;
    std::vector<uint32_t> spirvMS;
    std::vector<uint32_t> spirvFS;
    {
        auto source = LoadString("projects/118_mesh_shader_vertex_attrs/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        auto        hr = CompileHLSL(source, "asmain", "as_6_5", &spirvAS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (AS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "msmain", "ms_6_5", &spirvMS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (MS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "psmain", "ps_6_5", &spirvFS, &errorMsg);
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
    // Make them meshlets!
    // *************************************************************************
    std::vector<glm::vec3>       positions;
    std::vector<glm::vec2>       texCoords;
    std::vector<glm::vec3>       normals;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t>        meshletVertices;
    std::vector<uint8_t>         meshletTriangles;
    {
        TriMesh mesh = {};
        bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/full_horse_statue_01_1k.obj").string(), &mesh);
        if (!res)
        {
            assert(false && "failed to load model");
        }

        positions = mesh.GetPositions();
        texCoords = mesh.GetTexCoords();
        normals   = mesh.GetNormals();

        const size_t kMaxVertices  = 64;
        const size_t kMaxTriangles = 124;
        const float  kConeWeight   = 0.0f;

        const size_t maxMeshlets = meshopt_buildMeshletsBound(mesh.GetNumIndices(), kMaxVertices, kMaxTriangles);

        meshlets.resize(maxMeshlets);
        meshletVertices.resize(maxMeshlets * kMaxVertices);
        meshletTriangles.resize(maxMeshlets * kMaxTriangles * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshlets.data(),
            meshletVertices.data(),
            meshletTriangles.data(),
            reinterpret_cast<const uint32_t*>(mesh.GetTriangles().data()),
            mesh.GetNumIndices(),
            reinterpret_cast<const float*>(mesh.GetPositions().data()),
            mesh.GetNumVertices(),
            sizeof(glm::vec3),
            kMaxVertices,
            kMaxTriangles,
            kConeWeight);

        auto& last = meshlets[meshletCount - 1];
        meshletVertices.resize(last.vertex_offset + last.vertex_count);
        meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshletCount);
    }

    // Repack triangles from 3 consecutive byes to 4-byte uint32_t to
    // make it easier to unpack on the GPU.
    //
    std::vector<uint32_t> meshletTrianglesU32;
    for (auto& m : meshlets)
    {
        // Save triangle offset for current meshlet
        uint32_t triangleOffset = static_cast<uint32_t>(meshletTrianglesU32.size());

        // Repack to uint32_t
        for (uint32_t i = 0; i < m.triangle_count; ++i)
        {
            uint32_t i0 = 3 * i + 0 + m.triangle_offset;
            uint32_t i1 = 3 * i + 1 + m.triangle_offset;
            uint32_t i2 = 3 * i + 2 + m.triangle_offset;

            uint8_t  vIdx0  = meshletTriangles[i0];
            uint8_t  vIdx1  = meshletTriangles[i1];
            uint8_t  vIdx2  = meshletTriangles[i2];
            uint32_t packed = ((static_cast<uint32_t>(vIdx0) & 0xFF) << 0) |
                              ((static_cast<uint32_t>(vIdx1) & 0xFF) << 8) |
                              ((static_cast<uint32_t>(vIdx2) & 0xFF) << 16);
            meshletTrianglesU32.push_back(packed);
        }

        // Update triangle offset for current meshlet
        m.triangle_offset = triangleOffset;
    }

    VulkanBuffer positionBuffer;
    VulkanBuffer texCoordsBuffer;
    VulkanBuffer normalsBuffer;
    VulkanBuffer meshletBuffer;
    VulkanBuffer meshletVerticesBuffer;
    VulkanBuffer meshletTrianglesBuffer;
    {
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(positions), DataPtr(positions), usageFlags, 0, &positionBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(texCoords), DataPtr(texCoords), usageFlags, 0, &texCoordsBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(normals), DataPtr(normals), usageFlags, 0, &normalsBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshlets), DataPtr(meshlets), usageFlags, 0, &meshletBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletVertices), DataPtr(meshletVertices), usageFlags, 0, &meshletVerticesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletTrianglesU32), DataPtr(meshletTrianglesU32), usageFlags, 0, &meshletTrianglesBuffer));
    }

    // *************************************************************************
    // Pipeline layout
    //
    // This is used for pipeline creation
    //
    // *************************************************************************
    VkPipelineLayout      pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    CreatePipelineLayout(renderer.get(), &pipelineLayout, &descriptorSetLayout);

    // *************************************************************************
    // Shader module
    // *************************************************************************
    VkShaderModule moduleAS = VK_NULL_HANDLE;
    VkShaderModule moduleMS = VK_NULL_HANDLE;
    VkShaderModule moduleFS = VK_NULL_HANDLE;
    CreateShaderModules(
        renderer.get(),
        spirvAS,
        spirvMS,
        spirvFS,
        &moduleAS,
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
        moduleAS,
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
        assert(false && "GrexWindow::Create failed");
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
    std::vector<VkImage>     swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkImageView> swapchainDepthViews;
    {
        CHECK_CALL(GetSwapchainImages(renderer.get(), swapchainImages));

        for (auto& image : swapchainImages)
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

            swapchainImageViews.push_back(imageView);
        }

        size_t imageCount = swapchainImages.size();

        std::vector<VulkanImage> depthImages;
        depthImages.resize(swapchainImages.size());

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

            swapchainDepthViews.push_back(depthView);
        }
    }

    // *************************************************************************
    // Render pass to draw ImGui
    // *************************************************************************
    std::vector<VulkanAttachmentInfo> colorAttachmentInfos = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, renderer->SwapchainImageUsage}
    };

    VulkanRenderPass renderPass = {};
    CHECK_CALL(CreateRenderPass(renderer.get(), colorAttachmentInfos, {}, gWindowWidth, gWindowHeight, &renderPass));

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForVulkan(renderer.get(), renderPass.RenderPass))
    {
        assert(false && "GrexWindow::InitImGuiForD3D12 failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command buffer
    // *************************************************************************
    CommandObjects cmdBuf = {};
    {
        CHECK_CALL(CreateCommandBuffer(renderer.get(), 0, &cmdBuf));
    }

    // *************************************************************************
    // Scene and constant buffer
    // *************************************************************************
    SceneProperties scene = {};

    VulkanBuffer sceneBuffer;
    {
        size_t size = Align<size_t>(sizeof(SceneProperties), 256);
        CHECK_CALL(CreateBuffer(renderer.get(), sizeof(SceneProperties), nullptr, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0, &sceneBuffer));
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color        = {0.23f, 0.23f, 0.31f, 0};
    clearValues[1].depthStencil = {1.0f, 0};

    while (window->PollEvents())
    {
        window->ImGuiNewFrameVulkan();

        if (ImGui::Begin("Params"))
        {
            // Visibility Func
            static const char* currentDrawFuncName = gDrawFuncNames[gDrawFunc].c_str();
            if (ImGui::BeginCombo("Draw Func", currentDrawFuncName))
            {
                for (size_t i = 0; i < gDrawFuncNames.size(); ++i)
                {
                    bool isSelected = (currentDrawFuncName == gDrawFuncNames[i]);
                    if (ImGui::Selectable(gDrawFuncNames[i].c_str(), isSelected))
                    {
                        currentDrawFuncName = gDrawFuncNames[i].c_str();
                        gDrawFunc           = static_cast<uint32_t>(i);
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

        // Update scene
        {
            vec3 eyePosition = vec3(0, 0.105f, 0.40f);
            vec3 target      = vec3(0, 0.105f, 0);

            PerspCamera camera = PerspCamera(60.0f, window->GetAspectRatio(), 0.1f, 10000.0f);
            camera.LookAt(eyePosition, target);

            scene.InstanceM     = glm::rotate(static_cast<float>(glfwGetTime()), glm::vec3(0, 1, 0));
            scene.CameraVP      = camera.GetViewProjectionMatrix();
            scene.EyePosition   = eyePosition;
            scene.DrawFunc      = gDrawFunc;
            scene.LightPosition = vec3(0.25f, 1, 1);

            void* pDst = nullptr;
            CHECK_CALL(vmaMapMemory(renderer.get()->Allocator, sceneBuffer.Allocation, reinterpret_cast<void**>(&pDst)));
            memcpy(pDst, &scene, sizeof(SceneProperties));
            vmaUnmapMemory(renderer.get()->Allocator, sceneBuffer.Allocation);
        }

        // ---------------------------------------------------------------------

        uint32_t swapchainImageIndex = 0;
        if (AcquireNextImage(renderer.get(), &swapchainImageIndex))
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
                swapchainImages[swapchainImageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_PRESENT,
                RESOURCE_STATE_RENDER_TARGET);

            VkRenderingAttachmentInfo colorAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView                 = swapchainImageViews[swapchainImageIndex];
            colorAttachment.imageLayout               = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue                = clearValues[0];

            VkRenderingAttachmentInfo depthAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView                 = swapchainDepthViews[swapchainImageIndex];
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

            PerspCamera camera = PerspCamera(60.0f, window->GetAspectRatio());
            camera.LookAt(vec3(0, 0.105f, 0.40f), vec3(0, 0.105f, 0));

            mat4 R   = glm::rotate(static_cast<float>(glfwGetTime()), glm::vec3(0, 1, 0));
            mat4 MVP = camera.GetViewProjectionMatrix() * R;

            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &sceneBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &positionBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &texCoordsBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &normalsBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletVerticesBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletTrianglesBuffer);

            // Task (amplification) shader uses 32 for thread group size
            uint32_t threadGroupCountX = static_cast<UINT>((meshlets.size() / 32) + 1);
            fn_vkCmdDrawMeshTasksEXT(cmdBuf.CommandBuffer, threadGroupCountX, 1, 1);

            vkCmdEndRendering(cmdBuf.CommandBuffer);

            // ImGui
            {
                VkRenderPassAttachmentBeginInfo attachmentBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
                attachmentBeginInfo.pNext                           = 0;
                attachmentBeginInfo.attachmentCount                 = 1;
                attachmentBeginInfo.pAttachments                    = &swapchainImageViews[swapchainImageIndex];

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
                swapchainImages[swapchainImageIndex],
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
        }

        if (!SwapchainPresent(renderer.get(), swapchainImageIndex))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreatePipelineLayout(
    VulkanRenderer*        pRenderer,
    VkPipelineLayout*      pPipelineLayout,
    VkDescriptorSetLayout* pDescriptorSetLayout)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};

    // ConstantBuffer<SceneProperties> Cam : register(b0);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 0;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<float3> Positions : register(t1);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 1;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<float2> TexCoords : register(t2);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 2;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<float3> Normals : register(t3);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 3;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<Meshlet> Meshlets : register(t4);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 4;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // ByteAddressBuffer MeshletVertexIndices : register(t5);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 5;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<uint> MeshletTriangles : register(t6);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 6;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // Create descriptor set
    {
        VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        createInfo.bindingCount                    = CountU32(bindings);
        createInfo.pBindings                       = DataPtr(bindings);

        CHECK_CALL(vkCreateDescriptorSetLayout(pRenderer->Device, &createInfo, nullptr, pDescriptorSetLayout));
    }

    // Create pipeline layout
    {
        VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        createInfo.setLayoutCount             = 1;
        createInfo.pSetLayouts                = pDescriptorSetLayout;

        CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, pPipelineLayout));
    }
}

void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvAS,
    const std::vector<uint32_t>& spirvMS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleAS,
    VkShaderModule*              pModuleMS,
    VkShaderModule*              pModuleFS)
{
    // Amplification Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvAS);
        createInfo.pCode                    = DataPtr(spirvAS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleAS));
    }

    // Mesh Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvMS);
        createInfo.pCode                    = DataPtr(spirvMS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleMS));
    }

    // Fragment Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvFS);
        createInfo.pCode                    = DataPtr(spirvFS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleFS));
    }
}
