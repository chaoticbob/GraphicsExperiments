#include "window.h"

#include "vk_renderer.h"
#include "bitmap.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <fstream>

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

#define MATERIAL_TEXTURE_STRIDE 4
#define NUM_MATERIALS           16
#define TOTAL_MATERIAL_TEXTURES (NUM_MATERIALS * MATERIAL_TEXTURE_STRIDE)

#define IBL_INTEGRATION_LUT_DESCRIPTOR_OFFSET    3
#define IBL_INTEGRATION_MS_LUT_DESCRIPTOR_OFFSET 4
#define IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET    16
#define IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET   48
#define MATERIAL_TEXTURES_DESCRIPTOR_OFFSET      100

// This will be passed in via constant buffer
struct Light
{
    uint32_t active;
    vec3     position;
    vec3     color;
    float    intensity;
};

struct PBRSceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t numLights;
    Light    lights[8];
    uint32_t iblNumEnvLevels;
    uint32_t iblIndex;
    uint     multiscatter;
    uint     colorCorrect;
};

struct EnvSceneParameters
{
    mat4     MVP;
    uint32_t IBLIndex;
};

struct MaterialParameters
{
    float specular;
};

struct DrawParameters
{
    mat4     ModelMatrix;
    uint32_t MaterialIndex;
    uint32_t InvertNormalMapY;
};

struct MaterialTextures
{
    VulkanImage baseColorTexture;
    VulkanImage normalTexture;
    VulkanImage roughnessTexture;
    VulkanImage metallicTexture;
};

struct GeometryBuffers
{
    uint32_t     numIndices;
    VulkanBuffer indexBuffer;
    VulkanBuffer positionBuffer;
    VulkanBuffer texCoordBuffer;
    VulkanBuffer normalBuffer;
    VulkanBuffer tangentBuffer;
    VulkanBuffer bitangentBuffer;
};

// =============================================================================
// Constants
// =============================================================================

const std::vector<std::string> gModelNames = {
    "Sphere",
    "Knob",
    "Monkey",
    "Cube",
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static std::vector<std::string> gMaterialNames = {};

static uint32_t                 gNumLights  = 4;
static const uint32_t           gMaxIBLs    = 32;
static uint32_t                 gIBLIndex   = 0;
static std::vector<std::string> gIBLNames   = {};
static uint32_t                 gModelIndex = 0;

void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout);
void CreateEnvironmentPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout);
void CreateEnvironmentVertexBuffers(
    VulkanRenderer*  pRenderer,
    GeometryBuffers& outGeomtryBuffers);
void CreateMaterialModels(
    VulkanRenderer*               pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers);
void CreateIBLTextures(
    VulkanRenderer*           pRenderer,
    VulkanImage*              ppBRDFLUT,
    VulkanImage*              ppMultiscatterBRDFLUT,
    std::vector<VulkanImage>& outIrradianceTextures,
    std::vector<VulkanImage>& outEnvironmentTextures,
    std::vector<uint32_t>&    outEnvNumLevels);
void CreateMaterials(
    VulkanRenderer*                  pRenderer,
    MaterialTextures&                outDefaultMaterialTextures,
    std::vector<MaterialTextures>&   outMaterialTexturesSets,
    std::vector<MaterialParameters>& outMaterialParametersSets);
void CreateDescriptorBuffer(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout pDescriptorSetLayout,
    VulkanBuffer*         pBuffer);
void WritePBRDescriptors(
    VulkanRenderer*                pRenderer,
    VkDescriptorSetLayout          descriptorSetLayout,
    VulkanBuffer*                  pDescriptorBuffer,
    VulkanBuffer*                  pSceneParamsBuffer,
    VulkanBuffer*                  pMaterialBuffer,
    std::vector<MaterialTextures>& materialTextureSets,
    const VulkanImage*             pBRDFLUT,
    const VulkanImage*             pMultiscatterBRDFLUT,
    std::vector<VulkanImage>&      irrTextures,
    std::vector<VulkanImage>&      envTextures);
void WriteEnvDescriptors(
    VulkanRenderer*          pRenderer,
    VkDescriptorSetLayout    descriptorSetLayout,
    VulkanBuffer*            pDescriptorBuffer,
    std::vector<VulkanImage> envTextures);

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
    // PBR shaders
    std::vector<uint32_t> spirvVS;
    std::vector<uint32_t> spirvFS;
    {
        std::string shaderSource = LoadString("projects/253_pbr_material_textures/shaders.hlsl");
        if (shaderSource.empty())
        {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

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
        std::string shaderSource = LoadString("projects/253_pbr_material_textures/drawtexture.hlsl");
        if (shaderSource.empty())
        {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderSource, "vsmain", "vs_6_0", &drawTextureSpirvVS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(shaderSource, "psmain", "ps_6_0", &drawTextureSpirvFS, &errorMsg);
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
    VkPipeline pbrPipelineState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        pbrPipelineLayout.PipelineLayout,
        shaderModuleVS,
        shaderModuleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pbrPipelineState,
        VK_CULL_MODE_BACK_BIT));

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
        VK_CULL_MODE_FRONT_BIT,
        "vsmain",
        "psmain"));

    // *************************************************************************
    // Constant buffer
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
    // Environment vertex buffers
    // *************************************************************************
    GeometryBuffers envGeoBuffers;
    CreateEnvironmentVertexBuffers(
        renderer.get(),
        envGeoBuffers);

    // *************************************************************************
    // Material models
    // *************************************************************************
    std::vector<GeometryBuffers> matGeoBuffers;
    CreateMaterialModels(
        renderer.get(),
        matGeoBuffers);

    // *************************************************************************
    // Environment texture
    // *************************************************************************
    VulkanImage              brdfLUT;
    VulkanImage              multiscatterBRDFLUT;
    std::vector<VulkanImage> irrTextures;
    std::vector<VulkanImage> envTextures;
    std::vector<uint32_t>    envNumLevels;
    CreateIBLTextures(
        renderer.get(),
        &brdfLUT,
        &multiscatterBRDFLUT,
        irrTextures,
        envTextures,
        envNumLevels);

    // *************************************************************************
    // Material texture
    // *************************************************************************
    MaterialTextures                defaultMaterialTextures;
    std::vector<MaterialTextures>   materialTexturesSets;
    std::vector<MaterialParameters> materialParametersSets;
    CreateMaterials(
        renderer.get(),
        defaultMaterialTextures,
        materialTexturesSets,
        materialParametersSets);

    // *************************************************************************
    // Material buffer
    // *************************************************************************
    VulkanBuffer materialBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        SizeInBytes(materialParametersSets),
        DataPtr(materialParametersSets),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        0,
        &materialBuffer));

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
        &materialBuffer,
        materialTexturesSets,
        &brdfLUT,
        &multiscatterBRDFLUT,
        irrTextures,
        envTextures);

    VulkanBuffer envDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), envPipelineLayout.DescriptorSetLayout, &envDescriptorBuffer);

    WriteEnvDescriptors(
        renderer.get(),
        envPipelineLayout.DescriptorSetLayout,
        &envDescriptorBuffer,
        envTextures);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "253_pbr_material_textures_vulkan");
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
    // Persistent map parameters
    // *************************************************************************
    PBRSceneParameters* pPBRSceneParams = nullptr;
    vmaMapMemory(renderer->Allocator, pbrSceneParamsBuffer.Allocation, reinterpret_cast<void**>(&pPBRSceneParams));

    MaterialParameters* pMaterialParams = nullptr;
    vmaMapMemory(renderer->Allocator, materialBuffer.Allocation, reinterpret_cast<void**>(&pMaterialParams));

    // *************************************************************************
    // Set some scene params
    // *************************************************************************
    pPBRSceneParams->numLights           = gNumLights;
    pPBRSceneParams->lights[0].active    = 0;
    pPBRSceneParams->lights[0].position  = vec3(3, 10, 0);
    pPBRSceneParams->lights[0].color     = vec3(1, 1, 1);
    pPBRSceneParams->lights[0].intensity = 1.5f;
    pPBRSceneParams->lights[1].active    = 0;
    pPBRSceneParams->lights[1].position  = vec3(-8, 1, 4);
    pPBRSceneParams->lights[1].color     = vec3(0.85f, 0.95f, 0.81f);
    pPBRSceneParams->lights[1].intensity = 0.4f;
    pPBRSceneParams->lights[2].active    = 0;
    pPBRSceneParams->lights[2].position  = vec3(0, 8, -8);
    pPBRSceneParams->lights[2].color     = vec3(0.89f, 0.89f, 0.97f);
    pPBRSceneParams->lights[2].intensity = 0.95f;
    pPBRSceneParams->lights[3].active    = 0;
    pPBRSceneParams->lights[3].position  = vec3(15, 0, 0);
    pPBRSceneParams->lights[3].color     = vec3(0.92f, 0.5f, 0.7f);
    pPBRSceneParams->lights[3].intensity = 0.5f;
    pPBRSceneParams->iblNumEnvLevels     = envNumLevels[gIBLIndex];
    pPBRSceneParams->iblIndex            = gIBLIndex;
    pPBRSceneParams->colorCorrect        = 0;

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
            static const char* currentModelName = gModelNames[0].c_str();
            if (ImGui::BeginCombo("Model", currentModelName))
            {
                for (size_t i = 0; i < gModelNames.size(); ++i)
                {
                    bool isSelected = (currentModelName == gModelNames[i]);
                    if (ImGui::Selectable(gModelNames[i].c_str(), isSelected))
                    {
                        currentModelName = gModelNames[i].c_str();
                        gModelIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName))
            {
                for (size_t i = 0; i < gIBLNames.size(); ++i)
                {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected))
                    {
                        currentIBLName            = gIBLNames[i].c_str();
                        pPBRSceneParams->iblIndex = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            ImGui::Checkbox("Multiscatter", reinterpret_cast<bool*>(&pPBRSceneParams->multiscatter));

            ImGui::Separator();

            ImGui::Checkbox("Color Correct", reinterpret_cast<bool*>(&pPBRSceneParams->colorCorrect));

            ImGui::Separator();

            for (uint32_t lightIdx = 0; lightIdx < 4; ++lightIdx)
            {
                std::stringstream lightName;
                lightName << "Light " << lightIdx;
                if (ImGui::TreeNodeEx(lightName.str().c_str(), ImGuiTreeNodeFlags_None))
                {
                    ImGui::Checkbox("Active", reinterpret_cast<bool*>(&pPBRSceneParams->lights[lightIdx].active));
                    ImGui::SliderFloat("Intensity", &pPBRSceneParams->lights[lightIdx].intensity, 0.0f, 10.0f);
                    ImGui::ColorPicker3("Albedo", reinterpret_cast<float*>(&(pPBRSceneParams->lights[lightIdx].color)), ImGuiColorEditFlags_NoInputs);

                    ImGui::TreePop();
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("Material Parameters"))
        {
            for (uint32_t matIdx = 0; matIdx < gMaterialNames.size(); ++matIdx)
            {
                if (ImGui::TreeNodeEx(gMaterialNames[matIdx].c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::SliderFloat("Specular", &(pMaterialParams[matIdx].specular), 0.0f, 1.0f);

                    ImGui::TreePop();
                }

                ImGui::Separator();
            }
        }
        ImGui::End();

        // ---------------------------------------------------------------------

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

            VkViewport viewport = {0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {0, 0, gWindowWidth, gWindowHeight};
            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

            // Smooth out the rotation on Y
            gAngle += (gTargetAngle - gAngle) * 0.1f;

            // Camera matrices - spin the camera around the target
            mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
            vec3 startingEyePosition = vec3(0, 2.5f, 10);
            vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
            mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

            // Set scene params values that required calculation
            pPBRSceneParams->viewProjectionMatrix = projMat * viewMat;
            pPBRSceneParams->eyePosition          = eyePosition;
            pPBRSceneParams->iblNumEnvLevels      = envNumLevels[gIBLIndex];

            // Draw environment
            {
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
                    envPipelineLayout.PipelineLayout,
                    0, // firstSet
                    1, // setCount
                    &bufferIndices,
                    &descriptorBufferOffsets);

                // Bind the VS/FS Graphics Pipeline
                vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, envPipelineState);

                glm::mat4 moveUp = glm::translate(vec3(0, 5, 0));

                // SceneParmas (b0)
                mat4 mvp = projMat * viewMat * moveUp;
                vkCmdPushConstants(cmdBuf.CommandBuffer, envPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &mvp);
                vkCmdPushConstants(cmdBuf.CommandBuffer, envPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &pPBRSceneParams->iblIndex);

                // Bind the Index Buffer
                vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, envGeoBuffers.indexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

                // Bind the Vertex Buffer
                VkBuffer     vertexBuffers[] = {envGeoBuffers.positionBuffer.Buffer, envGeoBuffers.texCoordBuffer.Buffer};
                VkDeviceSize offsets[]       = {0, 0};
                vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 2, vertexBuffers, offsets);

                vkCmdDrawIndexed(cmdBuf.CommandBuffer, envGeoBuffers.numIndices, 1, 0, 0, 0);
            }

            // Draw material models
            {
                VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
                descriptorBufferBindingInfo.pNext                            = nullptr;
                descriptorBufferBindingInfo.address                          = GetDeviceAddress(renderer.get(), &pbrDescriptorBuffer);
                descriptorBufferBindingInfo.usage                            = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
                fn_vkCmdBindDescriptorBuffersEXT(cmdBuf.CommandBuffer, 1, &descriptorBufferBindingInfo);

                uint32_t     bufferIndices           = 0;
                VkDeviceSize descriptorBufferOffsets = 0;
                fn_vkCmdSetDescriptorBufferOffsetsEXT(
                    cmdBuf.CommandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pbrPipelineLayout.PipelineLayout,
                    0, // firstSet
                    1, // setCount
                    &bufferIndices,
                    &descriptorBufferOffsets);

                // Select which model to draw
                const GeometryBuffers& geoBuffers = matGeoBuffers[gModelIndex];

                // Index buffer
                vkCmdBindIndexBuffer(cmdBuf.CommandBuffer, geoBuffers.indexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

                // Vertex buffers
                VkBuffer vertexBuffers[] = {
                    geoBuffers.positionBuffer.Buffer,
                    geoBuffers.texCoordBuffer.Buffer,
                    geoBuffers.normalBuffer.Buffer,
                    geoBuffers.tangentBuffer.Buffer,
                    geoBuffers.bitangentBuffer.Buffer,
                };
                VkDeviceSize offsets[] = {0, 0, 0, 0, 0};
                vkCmdBindVertexBuffers(cmdBuf.CommandBuffer, 0, 5, vertexBuffers, offsets);

                // Pipeline state
                vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipelineState);

                const float yPos             = 0.0f;
                uint32_t    materialIndex    = 0;
                uint32_t    invertNormalMapY = false; // Invert if sphere

                // Material 0
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, 4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 1
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, 4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 2
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, 4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 3
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, 4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 4
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, 1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 5
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, 1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 6
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, 1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 7
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, 1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 8
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, -1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 9
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, -1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 10
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, -1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 11
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, -1.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 12
                {
                    glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, -4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 13
                {
                    glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, -4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 14
                {
                    glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, -4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }

                // Material 15
                {
                    glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, -4.5f));

                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(glm::mat4), &modelMat);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4), sizeof(uint32_t), &materialIndex);
                    vkCmdPushConstants(cmdBuf.CommandBuffer, pbrPipelineLayout.PipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(glm::mat4) + sizeof(uint32_t), sizeof(uint32_t), &invertNormalMapY);
                    vkCmdDrawIndexed(cmdBuf.CommandBuffer, geoBuffers.numIndices, 1, 0, 0, 0);

                    if (materialIndex < (materialTexturesSets.size() - 1))
                    {
                        ++materialIndex;
                    }
                }
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

void CreatePBRPipeline(VulkanRenderer* pRenderer, VulkanPipelineLayout* pLayout)
{
    // Descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {};
        // ConstantBuffer<SceneParameters>      SceneParams                   : register(b0);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 0;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }

        // DEFINE_AS_PUSH_CONSTANT
        // ConstantBuffer<DrawParameters>       DrawParams                    : register(b1);

        // StructuredBuffer<MaterialParameters> MaterialParams                : register(t2);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 2;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                            IBLIntegrationLUT             : register(t3);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 3;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                            IBLIntegrationMultiscatterLUT : register(t4);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 4;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                            IBLIrradianceMaps[32]         : register(t16);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 16;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 32;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                            IBLEnvironmentMaps[32]        : register(t48);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 48;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 32;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                         IBLIntegrationSampler         : register(s32);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 32;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState                         IBLMapSampler                 : register(s33);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 33;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D    MaterialTextures[TOTAL_MATERIAL_TEXTURES] : register(t100);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 100;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = TOTAL_MATERIAL_TEXTURES;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState MaterialSampler                           : register(s34);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 34;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // SamplerState MaterialNormalMapSampler                  : register(s35);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 35;
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
        push_constant.size                = sizeof(DrawParameters);
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
        // DEFINE_AS_PUSH_CONSTANT
        // ConstantBuffer<SceneParameters> SceneParmas  : register(b0);

        // SamplerState                    Sampler0     : register(s1);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 1;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL_GRAPHICS;
            bindings.push_back(binding);
        }
        // Texture2D                       Textures[16] : register(t32);
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 32;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = 16;
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

void CreateEnvironmentVertexBuffers(
    VulkanRenderer*  pRenderer,
    GeometryBuffers& outGeomtryBuffers)
{
    TriMesh mesh = TriMesh::Sphere(25, 64, 64, {.enableTexCoords = true, .faceInside = true});

    outGeomtryBuffers.numIndices = 3 * mesh.GetNumTriangles();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        &outGeomtryBuffers.indexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        &outGeomtryBuffers.positionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        &outGeomtryBuffers.texCoordBuffer));
}

void CreateMaterialModels(
    VulkanRenderer*               pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers)
{
    // Sphere
    {
        TriMesh::Options options = {.enableTexCoords = true, .enableNormals = true, .enableTangents = true};

        TriMesh mesh = TriMesh::Sphere(1, 256, 256, options);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Knob
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.invertTexCoordsV = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh))
        {
            return;
        }
        mesh.ScaleToFit(1.0f);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Monkey
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/monkey.obj").string(), "", options, &mesh))
        {
            return;
        }
        // mesh.ScaleToUnit();

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Cube
    {
        TriMesh mesh = TriMesh::Cube(vec3(2), false, {.enableTexCoords = true, .enableNormals = true, .enableTangents = true});

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }
}

void CreateIBLTextures(
    VulkanRenderer*           pRenderer,
    VulkanImage*              pBRDFLUT,
    VulkanImage*              pMultiscatterBRDFLUT,
    std::vector<VulkanImage>& outIrradianceTextures,
    std::vector<VulkanImage>& outEnvironmentTextures,
    std::vector<uint32_t>&    outEnvNumLevels)
{
    // BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
        if (bitmap.Empty())
        {
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
        if (bitmap.Empty())
        {
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

    auto                               iblDir = GetAssetPath("IBL");
    std::vector<std::filesystem::path> iblFiles;
    for (auto& entry : std::filesystem::directory_iterator(iblDir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        auto path = entry.path();
        auto ext  = path.extension();
        if (ext == ".ibl")
        {
            path = std::filesystem::relative(path, iblDir.parent_path());
            iblFiles.push_back(path);
        }
    }

    size_t maxEntries = std::min<size_t>(gMaxIBLs, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i)
    {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl))
        {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            assert(false && "IBL maps load failed failed");
            return;
        }

        outEnvNumLevels.push_back(ibl.numLevels);

        // Irradiance
        {
            VulkanImage texture = {};
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.irradianceMap.GetWidth(),
                ibl.irradianceMap.GetHeight(),
                VK_FORMAT_R32G32B32A32_SFLOAT,
                ibl.irradianceMap.GetSizeInBytes(),
                ibl.irradianceMap.GetPixels(),
                &texture));
            outIrradianceTextures.push_back(texture);
        }

        // Environment
        {
            const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
            const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

            std::vector<MipOffset> mipOffsets;
            uint32_t               levelOffset = 0;
            uint32_t               levelWidth  = ibl.baseWidth;
            uint32_t               levelHeight = ibl.baseHeight;
            for (uint32_t i = 0; i < ibl.numLevels; ++i)
            {
                MipOffset mipOffset = {};
                mipOffset.Offset    = levelOffset;
                mipOffset.RowStride = rowStride;

                mipOffsets.push_back(mipOffset);

                levelOffset += (rowStride * levelHeight);
                levelWidth >>= 1;
                levelHeight >>= 1;
            }

            VulkanImage texture = {};
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.baseWidth,
                ibl.baseHeight,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                mipOffsets,
                ibl.environmentMap.GetSizeInBytes(),
                ibl.environmentMap.GetPixels(),
                &texture));
            outEnvironmentTextures.push_back(texture);
        }

        gIBLNames.push_back(iblFile.filename().replace_extension().string());

        GREX_LOG_INFO("Loaded " << iblFile);
    }
}

void CreateMaterials(
    VulkanRenderer*                  pRenderer,
    MaterialTextures&                outDefaultMaterialTextures,
    std::vector<MaterialTextures>&   outMaterialTexturesSets,
    std::vector<MaterialParameters>& outMaterialParametersSets)
{
    // Default material textures
    {
        PixelRGBA8u purplePixel = {0, 0, 0, 255};
        PixelRGBA8u blackPixel  = {0, 0, 0, 255};
        PixelRGBA8u whitePixel  = {255, 255, 255, 255};

        CHECK_CALL(CreateTexture(pRenderer, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &purplePixel, &outDefaultMaterialTextures.baseColorTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.normalTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.roughnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.metallicTexture));
    }

    // Texture directory
    auto texturesDir = GetAssetPath("textures");

    // Material files - limit to 16 since there's 16 objects draws
    std::vector<std::filesystem::path> materialFiles = {
        texturesDir / "bark_brown_02" / "material.mat",
        texturesDir / "bark_willow" / "material.mat",
        texturesDir / "brick_4" / "material.mat",
        texturesDir / "castle_brick_02_red" / "material.mat",
        texturesDir / "dark_brick_wall" / "material.mat",
        texturesDir / "factory_wall" / "material.mat",
        texturesDir / "green_metal_rust" / "material.mat",
        texturesDir / "hexagonal_concrete_paving" / "material.mat",
        texturesDir / "metal_grate_rusty" / "material.mat",
        texturesDir / "metal_plate" / "material.mat",
        texturesDir / "mud_cracked_dry_riverbed_002" / "material.mat",
        texturesDir / "pavement_02" / "material.mat",
        texturesDir / "rough_plaster_broken" / "material.mat",
        texturesDir / "rusty_metal_02" / "material.mat",
        texturesDir / "weathered_planks" / "material.mat",
        texturesDir / "wood_table_001" / "material.mat",
    };

    size_t maxEntries = materialFiles.size();
    for (size_t i = 0; i < maxEntries; ++i)
    {
        auto materialFile = materialFiles[i];

        std::ifstream is = std::ifstream(materialFile.string().c_str());
        if (!is.is_open())
        {
            assert(false && "faild to open material file");
        }

        MaterialTextures   materialTextures = outDefaultMaterialTextures;
        MaterialParameters materialParams   = {};

        while (!is.eof())
        {
            VulkanImage*          pTargetTexture = {};
            std::filesystem::path textureFile    = "";

            std::string key;
            is >> key;
            if (key == "basecolor")
            {
                is >> textureFile;
                pTargetTexture = &materialTextures.baseColorTexture;
            }
            else if (key == "normal")
            {
                is >> textureFile;
                pTargetTexture = &materialTextures.normalTexture;
            }
            else if (key == "roughness")
            {
                is >> textureFile;
                pTargetTexture = &materialTextures.roughnessTexture;
            }
            else if (key == "metallic")
            {
                is >> textureFile;
                pTargetTexture = &materialTextures.metallicTexture;
            }
            else if (key == "specular")
            {
                is >> materialParams.specular;
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
                    &(*pTargetTexture)));

                GREX_LOG_INFO("Created texture from " << textureFile);
            }
            else
            {
                GREX_LOG_ERROR("Failed to load: " << textureFile);
                assert(false && "Failed to load texture!");
            }
        }

        outMaterialTexturesSets.push_back(materialTextures);
        outMaterialParametersSets.push_back(materialParams);

        // Use directory name for material name
        gMaterialNames.push_back(materialFile.parent_path().filename().string());
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
    VulkanRenderer*                pRenderer,
    VkDescriptorSetLayout          descriptorSetLayout,
    VulkanBuffer*                  pDescriptorBuffer,
    VulkanBuffer*                  pSceneParamsBuffer,
    VulkanBuffer*                  pMaterialBuffer,
    std::vector<MaterialTextures>& materialTextureSets,
    const VulkanImage*             pBRDFLUT,
    const VulkanImage*             pMultiscatterBRDFLUT,
    std::vector<VulkanImage>&      irrTextures,
    std::vector<VulkanImage>&      envTextures)
{
    char* pDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        pRenderer->Allocator,
        pDescriptorBuffer->Allocation,
        reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // ConstantBuffer<SceneParameters>      SceneParams                                : register(b0);
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        0, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        pSceneParamsBuffer);

    // DEFINE_AS_PUSH_CONSTANT
    // ConstantBuffer<DrawParameters>       DrawParams                                 : register(b1);

    // StructuredBuffer<MaterialParameters> MaterialParams                             : register(t2);
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        2, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        pMaterialBuffer);

    // Texture2D                            IBLIntegrationLUT                          : register(t3);
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

    // Texture2D                            IBLIntegrationMultiscatterLUT              : register(t4);
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

    // Texture2D                            IBLIrradianceMaps[32]                      : register(t16);
    {
        uint32_t arrayIndex = 0;
        for (auto& image : irrTextures)
        {
            VkImageView imageView = VK_NULL_HANDLE;
            CHECK_CALL(CreateImageView(
                pRenderer,
                &image,
                VK_IMAGE_VIEW_TYPE_2D,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                GREX_ALL_SUBRESOURCES,
                &imageView));

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                16,         // binding
                arrayIndex, // arrayElement
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                imageView,
                VK_IMAGE_LAYOUT_GENERAL);

            arrayIndex++;
        }
    }

    // Texture2D                            IBLEnvironmentMaps[32]                     : register(t48);
    {
        uint32_t arrayIndex = 0;
        for (auto& image : envTextures)
        {
            VkImageView imageView = VK_NULL_HANDLE;
            CHECK_CALL(CreateImageView(
                pRenderer,
                &image,
                VK_IMAGE_VIEW_TYPE_2D,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                GREX_ALL_SUBRESOURCES,
                &imageView));

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                48,         // binding
                arrayIndex, // arrayElement
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                imageView,
                VK_IMAGE_LAYOUT_GENERAL);

            arrayIndex++;
        }
    }

    // SamplerState                         IBLIntegrationSampler                      : register(s32);
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

    // SamplerState                         IBLMapSampler                              : register(s33);
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

    // Texture2D                            MaterialTextures[TOTAL_MATERIAL_TEXTURES]  : register(t100);
    {
        uint32_t arrayIndex = 0;
        for (auto& materialTextures : materialTextureSets)
        {
            VulkanImage* textureImages[] = {
                &materialTextures.baseColorTexture,
                &materialTextures.normalTexture,
                &materialTextures.roughnessTexture,
                &materialTextures.metallicTexture};

            for (auto& image : textureImages)
            {
                VkImageView imageView = VK_NULL_HANDLE;
                CHECK_CALL(CreateImageView(
                    pRenderer,
                    image,
                    VK_IMAGE_VIEW_TYPE_2D,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    GREX_ALL_SUBRESOURCES,
                    &imageView));

                WriteDescriptor(
                    pRenderer,
                    pDescriptorBufferStartAddress,
                    descriptorSetLayout,
                    100,        // binding
                    arrayIndex, // arrayElement
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    imageView,
                    VK_IMAGE_LAYOUT_GENERAL);

                arrayIndex++;
            }
        }
    }

    // SamplerState                         MaterialSampler                            : register(s34);
    {
        VkSamplerCreateInfo samplerInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.flags                   = 0;
        samplerInfo.magFilter               = VK_FILTER_LINEAR;
        samplerInfo.minFilter               = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
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

        VkSampler materialSampler = VK_NULL_HANDLE;
        CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &materialSampler));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            34, // binding
            0,  // arrayElement
            materialSampler);
    }

    // SamplerState                         MaterialNormalMapSampler                   : register(s35);
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

        VkSampler materialNormalSampler = VK_NULL_HANDLE;
        CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &materialNormalSampler));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            35, // binding
            0,  // arrayElement
            materialNormalSampler);
    }

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}

void WriteEnvDescriptors(
    VulkanRenderer*          pRenderer,
    VkDescriptorSetLayout    descriptorSetLayout,
    VulkanBuffer*            pDescriptorBuffer,
    std::vector<VulkanImage> envTextures)
{
    char* pDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        pRenderer->Allocator,
        pDescriptorBuffer->Allocation,
        reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // DEFINE_AS_PUSH_CONSTANT
    // ConstantBuffer<SceneParameters> SceneParmas  : register(b0);

    // SamplerState                    Sampler0     : register(s1);
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

        VkSampler uWrapSampler = VK_NULL_HANDLE;
        CHECK_CALL(vkCreateSampler(
            pRenderer->Device,
            &samplerInfo,
            nullptr,
            &uWrapSampler));

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            1, // binding
            0, // arrayElement
            uWrapSampler);
    }

    // Texture2D                       Textures[16] : register(t32);
    {
        uint32_t arrayIndex = 0;
        for (auto& image : envTextures)
        {
            VkImageView imageView = VK_NULL_HANDLE;
            CHECK_CALL(CreateImageView(
                pRenderer,
                &image,
                VK_IMAGE_VIEW_TYPE_2D,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                GREX_ALL_SUBRESOURCES,
                &imageView));

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                32,         // binding
                arrayIndex, // arrayElement
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                imageView,
                VK_IMAGE_LAYOUT_GENERAL);

            arrayIndex++;
        }
    }

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}
