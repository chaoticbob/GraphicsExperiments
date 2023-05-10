#include "window.h"

#include "vk_renderer.h"
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

// =============================================================================
// Macros, enums, and constants
// =============================================================================
const uint32_t kOutputResourcesOffset = 0;
const uint32_t kGeoBuffersOffset      = 20;
const uint32_t kIBLTextureOffset      = 100;
const uint32_t kMaxIBLs               = 100;
const uint32_t kMaxGeometries         = 25;

// =============================================================================
// Shader code
// =============================================================================
const char* gClearRayGenSamplesShader = R"(

RWTexture2D<float4>      AccumTarget   : register(u0); // Accumulation texture
RWStructuredBuffer<uint> RayGenSamples : register(u1); // Ray generation samples

[numthreads(8, 8, 1)]
void csmain(uint3 tid : SV_DispatchThreadId)
{
    AccumTarget[tid.xy] = float4(0, 0, 0, 0);

    uint idx = tid.y * 1920 + tid.x;
    RayGenSamples[idx] = 0;    
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth        = 1920;
static uint32_t gWindowHeight       = 1080;
static bool     gEnableDebug        = false;
static bool     gEnableRayTracing   = true;
static uint32_t gUniformmBufferSize = 256;

static const char* gRayGenShaderName     = "MyRaygenShader";
static const char* gMissShaderName       = "MyMissShader";
static const char* gClosestHitShaderName = "MyClosestHitShader";

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static std::vector<std::string> gMaterialNames = {};
static std::vector<std::string> gIBLNames      = {};

static uint32_t gIBLIndex           = 0;
static uint32_t gCurrentIBLIndex    = 0xFFFFFFFF;
static bool     gResetRayGenSamples = true;
static uint32_t gMaxSamples         = 4096;
static uint32_t gCurrentMaxSamples  = 0;

struct Light
{
    vec3  Position;
    vec3  Color;
    float Intensity;
};

struct SceneParameters
{
    mat4     ViewInverseMatrix;
    mat4     ProjectionInverseMatrix;
    mat4     ViewProjectionMatrix;
    vec3     EyePosition;
    uint32_t IBLIndex;
    uint     MaxSamples;
    uint     NumLights;
    Light    Lights[8];
};

struct Geometry
{
    uint32_t     indexCount;
    VulkanBuffer indexBuffer;
    uint32_t     vertexCount;
    VulkanBuffer positionBuffer;
    VulkanBuffer normalBuffer;
};

struct IBLTextures
{
    VulkanImage irrTexture;
    VulkanImage envTexture;
    uint32_t    envNumLevels;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    float metallic;
    float specularReflectance;
    float ior;
    vec3  emissionColor;
};

void CreateRayTracePipelineLayout(
    VulkanRenderer*       pRenderer,
    VkSampler*            pImmutableSampler,
    VulkanPipelineLayout* pPipelineLayout);
void CreateRayTracingPipeline(
    VulkanRenderer*             pRenderer,
    VkShaderModule              rayTraceModule,
    const VulkanPipelineLayout& pipelineLayout,
    VkPipeline*                 pPipeline);
void CreateShaderBindingTables(
    VulkanRenderer*                                  pRenderer,
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
    VkPipeline                                       pipeline,
    VulkanBuffer*                                    pRayGenSBT,
    VulkanBuffer*                                    pMissSBT,
    VulkanBuffer*                                    pHitGroupSBT);
void CreateGeometries(
    VulkanRenderer* pRenderer,
    Geometry&       outSphereGeometry,
    Geometry&       outKnobGeometry,
    Geometry&       outMonkeyGeometry,
    Geometry&       outTeapotGeometry,
    Geometry&       outBoxGeometry);
void CreateBLASes(
    VulkanRenderer*    pRenderer,
    const Geometry&    sphereGeometry,
    const Geometry&    knobGeometry,
    const Geometry&    monkeyGeometry,
    const Geometry&    teapotGeometry,
    const Geometry&    boxGeometry,
    VulkanAccelStruct* pSphereBLAS,
    VulkanAccelStruct* pKnobBLAS,
    VulkanAccelStruct* pMonkeyBLAS,
    VulkanAccelStruct* pTeapotBLAS,
    VulkanAccelStruct* pBoxBLAS);
void CreateTLAS(
    VulkanRenderer*                  pRenderer,
    const VulkanAccelStruct&         sphereBLAS,
    const VulkanAccelStruct&         knobBLAS,
    const VulkanAccelStruct&         monkeyBLAS,
    const VulkanAccelStruct&         teapotBLAS,
    const VulkanAccelStruct&         boxBLAS,
    VulkanAccelStruct*               pTLAS,
    std::vector<MaterialParameters>& outMaterialParams);
void CreateAccumTexture(VulkanRenderer* pRenderer, VulkanImage* pBuffer);
void CreateIBLTextures(
    VulkanRenderer*           pRenderer,
    std::vector<IBLTextures>& outIBLTextures);
void CreateDescriptorBuffer(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pBuffer);
void WriteDescriptors(
    VulkanRenderer*                 pRenderer,
    VkDescriptorSetLayout           descriptorSetLayout,
    VulkanBuffer*                   pDescriptorBuffer,
    const VulkanBuffer&             sceneParamsBuffer,
    const VulkanAccelStruct&        accelStruct,
    const VulkanImage&              accumTexture,
    const VulkanBuffer&             rayGenSamplesBuffer,
    const Geometry&                 sphereGeometry,
    const Geometry&                 knobGeometry,
    const Geometry&                 monkeyGeometry,
    const Geometry&                 teapotGeometry,
    const Geometry&                 boxGeometry,
    const VulkanBuffer&             materialParamsBuffer,
    const std::vector<IBLTextures>& iblTextures,
    VkImageView*                    pAccumImageView,
    std::vector<VkImageView>*       pIBLImageViews);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT) {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;

        gResetRayGenSamples = true;
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
    // Get ray tracing properties
    // *************************************************************************
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    {
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &rayTracingProperties;
        vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
    }

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
    // Compile shaders
    // *************************************************************************
    std::vector<uint32_t> rayTraceSpv;
    {
        auto source = LoadString("projects/033_034_raytracing_path_trace_pbr/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(source, "", "lib_6_5", &rayTraceSpv, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (raytracing): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    std::vector<uint32_t> clearRayGenDxil;
    {
        std::string errorMsg;
        HRESULT     hr = CompileHLSL(gClearRayGenSamplesShader, "csmain", "cs_6_5", &clearRayGenDxil, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (clear ray gen): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Ray tracing descriptor set and pipeline layout
    //
    // This is used for pipeline creation and setting the descriptor buffer(s).
    //
    // *************************************************************************
    VkSampler            immutableSampler       = VK_NULL_HANDLE;
    VulkanPipelineLayout rayTracePipelineLayout = {};
    CreateRayTracePipelineLayout(renderer.get(), &immutableSampler, &rayTracePipelineLayout);

    // *************************************************************************
    // Ray tracing Shader module
    // *************************************************************************
    VkShaderModule rayTraceShaderModule = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(rayTraceSpv);
        createInfo.pCode                    = DataPtr(rayTraceSpv);

        CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &rayTraceShaderModule));
    }

    // *************************************************************************
    // Ray tracing pipeline
    //
    // The pipeline is created with 3 shader groups:
    //    1) Ray gen
    //    2) Miss
    //    3) Hitgroup
    //
    // *************************************************************************
    VkPipeline rayTracePipeline = VK_NULL_HANDLE;
    CreateRayTracingPipeline(
        renderer.get(),
        rayTraceShaderModule,
        rayTracePipelineLayout,
        &rayTracePipeline);

    // *************************************************************************
    // Shader binding tables
    //
    // This assumes that there are 3 shader groups in the pipeline:
    //    1) Ray gen
    //    2) Miss
    //    3) Hitgroup
    //
    // *************************************************************************
    VulkanBuffer rgenSBT = {};
    VulkanBuffer missSBT = {};
    VulkanBuffer hitgSBT = {};
    CreateShaderBindingTables(
        renderer.get(),
        rayTracingProperties,
        rayTracePipeline,
        &rgenSBT,
        &missSBT,
        &hitgSBT);

    // *************************************************************************
    // Clear ray gen pipeline
    // *************************************************************************
    VulkanPipelineLayout clearRayGenPipelineLayout = {};
    VkPipeline           clearRayGenPipeline       = VK_NULL_HANDLE;
    {
        // Descriptor set layout
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings = {
                {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT},
                {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
            };

            VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
            createInfo.bindingCount                    = CountU32(bindings);
            createInfo.pBindings                       = DataPtr(bindings);

            CHECK_CALL(vkCreateDescriptorSetLayout(renderer->Device, &createInfo, nullptr, &clearRayGenPipelineLayout.DescriptorSetLayout));
        }

        // Pipeline layout
        {
            VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            createInfo.flags                      = 0;
            createInfo.setLayoutCount             = 1;
            createInfo.pSetLayouts                = &clearRayGenPipelineLayout.DescriptorSetLayout;

            CHECK_CALL(vkCreatePipelineLayout(renderer->Device, &createInfo, nullptr, &clearRayGenPipelineLayout.PipelineLayout));
        }

        // Shader module
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        {
            VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            createInfo.flags                    = 0;
            createInfo.codeSize                 = SizeInBytes(clearRayGenDxil);
            createInfo.pCode                    = DataPtr(clearRayGenDxil);

            CHECK_CALL(vkCreateShaderModule(
                renderer->Device,
                &createInfo,
                nullptr,
                &shaderModule));
        }

        // Pipeline
        {
            VkComputePipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            createInfo.flags                       = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
            createInfo.stage                       = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            createInfo.stage.flags                 = 0;
            createInfo.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
            createInfo.stage.module                = shaderModule;
            createInfo.stage.pName                 = "csmain";
            createInfo.layout                      = clearRayGenPipelineLayout.PipelineLayout;

            CHECK_CALL(vkCreateComputePipelines(
                renderer->Device,
                VK_NULL_HANDLE,
                1,
                &createInfo,
                nullptr,
                &clearRayGenPipeline));
        }
    }

    // *************************************************************************
    // Create geometry
    // *************************************************************************
    Geometry sphereGeometry = {};
    Geometry knobGeometry   = {};
    Geometry monkeyGeometry = {};
    Geometry teapotGeometry = {};
    Geometry boxGeometry    = {};
    CreateGeometries(
        renderer.get(),
        sphereGeometry,
        knobGeometry,
        monkeyGeometry,
        teapotGeometry,
        boxGeometry);

    // *************************************************************************
    // Bottom level acceleration structure
    // *************************************************************************
    VulkanAccelStruct sphereBLAS = {};
    VulkanAccelStruct knobBLAS   = {};
    VulkanAccelStruct monkeyBLAS = {};
    VulkanAccelStruct teapotBLAS = {};
    VulkanAccelStruct boxBLAS    = {};
    CreateBLASes(
        renderer.get(),
        sphereGeometry,
        knobGeometry,
        monkeyGeometry,
        teapotGeometry,
        boxGeometry,
        &sphereBLAS,
        &knobBLAS,
        &monkeyBLAS,
        &teapotBLAS,
        &boxBLAS);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    VulkanAccelStruct               TLAS;
    std::vector<MaterialParameters> materialParams;
    CreateTLAS(
        renderer.get(),
        sphereBLAS,
        knobBLAS,
        monkeyBLAS,
        teapotBLAS,
        boxBLAS,
        &TLAS,
        materialParams);

    // *************************************************************************
    // Accumulation texture
    // *************************************************************************
    VulkanImage accumTexture = {};
    CreateAccumTexture(renderer.get(), &accumTexture);

    // *************************************************************************
    // Material params buffer
    // *************************************************************************
    VulkanBuffer materialParamsBuffer = {};
    CreateBuffer(
        renderer.get(),
        SizeInBytes(materialParams),
        DataPtr(materialParams),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        0,
        &materialParamsBuffer);

    // *************************************************************************
    // Scene params constant buffer
    // *************************************************************************
    VulkanBuffer sceneParamsBuffer = {};
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        Align<size_t>(sizeof(SceneParameters), 256),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        0,
        &sceneParamsBuffer));

    // *************************************************************************
    // Ray gen samples buffer
    // *************************************************************************
    VulkanBuffer rayGenSamplesBuffer = {};
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        (gWindowWidth * gWindowHeight * sizeof(uint32_t)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        &rayGenSamplesBuffer));

    // *************************************************************************
    // IBL textures
    // *************************************************************************
    std::vector<IBLTextures> iblTextures = {};
    CreateIBLTextures(
        renderer.get(),
        iblTextures);

    // *************************************************************************
    // Descriptor buffers
    // *************************************************************************
    VulkanBuffer rayTraceDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), rayTracePipelineLayout.DescriptorSetLayout, &rayTraceDescriptorBuffer);

    // Write descriptors
    VkImageView              accumImageView = VK_NULL_HANDLE;
    std::vector<VkImageView> iblImageViews;
    WriteDescriptors(
        renderer.get(),
        rayTracePipelineLayout.DescriptorSetLayout,
        &rayTraceDescriptorBuffer,
        sceneParamsBuffer,
        TLAS,
        accumTexture,
        rayGenSamplesBuffer,
        sphereGeometry,
        knobGeometry,
        monkeyGeometry,
        teapotGeometry,
        boxGeometry,
        materialParamsBuffer,
        iblTextures,
        &accumImageView,
        &iblImageViews);

    // Clear ray gen descriptor buffer
    VulkanBuffer clearRayGenDescriptorBuffer = {};
    CreateDescriptorBuffer(renderer.get(), clearRayGenPipelineLayout.DescriptorSetLayout, &clearRayGenDescriptorBuffer);

    // Write descriptors
    {
        char* pDescriptorBuffeStartAddress = nullptr;
        CHECK_CALL(vmaMapMemory(
            renderer->Allocator,
            clearRayGenDescriptorBuffer.Allocation,
            reinterpret_cast<void**>(&pDescriptorBuffeStartAddress)));

        WriteDescriptor(
            renderer.get(),
            pDescriptorBuffeStartAddress,
            clearRayGenPipelineLayout.DescriptorSetLayout,
            0, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            accumImageView,
            VK_IMAGE_LAYOUT_GENERAL);

        WriteDescriptor(
            renderer.get(),
            pDescriptorBuffeStartAddress,
            clearRayGenPipelineLayout.DescriptorSetLayout,
            1, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            &rayGenSamplesBuffer);

        vmaUnmapMemory(renderer->Allocator, clearRayGenDescriptorBuffer.Allocation);
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "034_raytracing_path_trace_pbr_vulkan");
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight(), 3)) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain image views
    // *************************************************************************
    std::vector<VkImage>     swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    {
        CHECK_CALL(GetSwapchainImages(renderer.get(), swapchainImages));

        for (auto& image : swapchainImages) {
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

            swapchainImageViews.push_back(imageView);
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
    if (!window->InitImGuiForVulkan(renderer.get(), renderPass.RenderPass)) {
        assert(false && "Window::InitImGuiForD3D12 failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command buffer and fence
    // *************************************************************************
    CommandObjects cmdBuf = {};
    {
        CHECK_CALL(CreateCommandBuffer(renderer.get(), 0, &cmdBuf));
    }

    // *************************************************************************
    // Persistent map scene parameters
    // *************************************************************************
    SceneParameters* pSceneParams = nullptr;
    CHECK_CALL(vmaMapMemory(renderer->Allocator, sceneParamsBuffer.Allocation, reinterpret_cast<void**>(&pSceneParams)));

    // *************************************************************************
    // Persistent map ray trace descriptor buffer
    // *************************************************************************
    char* pRayTraceDescriptorBuffeStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        renderer->Allocator,
        rayTraceDescriptorBuffer.Allocation,
        reinterpret_cast<void**>(&pRayTraceDescriptorBuffeStartAddress)));

    // *************************************************************************
    // Misc vars
    // *************************************************************************
    uint32_t sampleCount     = 0;
    float    rayGenStartTime = 0;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        window->ImGuiNewFrameVulkan();

        if (ImGui::Begin("Scene")) {
            ImGui::SliderInt("Max Samples Per Pixel", reinterpret_cast<int*>(&gMaxSamples), 1, 16384);

            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName)) {
                for (size_t i = 0; i < gIBLNames.size(); ++i) {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected)) {
                        currentIBLName = gIBLNames[i].c_str();
                        gIBLIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            float progress = sampleCount / static_cast<float>(gMaxSamples);
            char  buf[256] = {};
            sprintf(buf, "%d/%d Samples", sampleCount, gMaxSamples);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), buf);

            ImGui::Separator();

            float currentTime = static_cast<float>(glfwGetTime());
            float elapsedTime = currentTime - rayGenStartTime;

            ImGui::Text("Render time: %0.3f seconds", elapsedTime);
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        if (gCurrentMaxSamples != gMaxSamples) {
            gCurrentMaxSamples  = gMaxSamples;
            gResetRayGenSamples = true;
        }

        if (gCurrentIBLIndex != gIBLIndex) {
            gCurrentIBLIndex    = gIBLIndex;
            gResetRayGenSamples = true;
        }

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.1f;
        // Keep resetting until the angle is somewhat stable
        if (fabs(gTargetAngle - gAngle) > 0.1f) {
            gResetRayGenSamples = true;
        }

        // Camera matrices
        mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
        vec3 startingEyePosition = vec3(0, 4.0f, 8.5f);
        vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
        mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 3, 0), vec3(0, 1, 0));
        mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set constant buffer values
        pSceneParams->ViewInverseMatrix       = glm::inverse(viewMat);
        pSceneParams->ProjectionInverseMatrix = glm::inverse(projMat);
        pSceneParams->IBLIndex                = gCurrentIBLIndex;
        pSceneParams->EyePosition             = eyePosition;
        pSceneParams->MaxSamples              = gCurrentMaxSamples;

        // ---------------------------------------------------------------------
        // Acquire swapchain image index
        // ---------------------------------------------------------------------
        uint32_t swapchainImageIndex = 0;
        if (AcquireNextImage(renderer.get(), &swapchainImageIndex)) {
            assert(false && "AcquireNextImage failed");
            break;
        }

        // Update output texture (u1)
        //
        // Most Vulkan implementations support STORAGE_IMAGE so we can
        // write directly to the image and skip a copy.
        //
        WriteDescriptor(
            renderer.get(),
            pRayTraceDescriptorBuffeStartAddress,
            rayTracePipelineLayout.DescriptorSetLayout,
            1, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            swapchainImageViews[swapchainImageIndex],
            VK_IMAGE_LAYOUT_GENERAL);

        // ---------------------------------------------------------------------
        // Build command buffer to trace rays
        // ---------------------------------------------------------------------
        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        // Reset ray gen samples
        if (gResetRayGenSamples) {
            sampleCount     = 0;
            rayGenStartTime = static_cast<float>(glfwGetTime());

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, clearRayGenPipeline);

            VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
            descriptorBufferBindingInfo.pNext                            = nullptr;
            descriptorBufferBindingInfo.address                          = GetDeviceAddress(renderer.get(), &clearRayGenDescriptorBuffer);
            descriptorBufferBindingInfo.usage                            = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
            fn_vkCmdBindDescriptorBuffersEXT(cmdBuf.CommandBuffer, 1, &descriptorBufferBindingInfo);

            uint32_t     bufferIndices           = 0;
            VkDeviceSize descriptorBufferOffsets = 0;
            fn_vkCmdSetDescriptorBufferOffsetsEXT(
                cmdBuf.CommandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                clearRayGenPipelineLayout.PipelineLayout,
                0, // firstSet
                1, // setCount
                &bufferIndices,
                &descriptorBufferOffsets);

            vkCmdDispatch(cmdBuf.CommandBuffer, gWindowWidth / 8, gWindowHeight / 8, 1);

            gResetRayGenSamples = false;
        }

        // Trace rays
        {
            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                swapchainImages[swapchainImageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_PRESENT,
                RESOURCE_STATE_COMPUTE_UNORDERED_ACCESS);

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracePipeline);

            VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
            descriptorBufferBindingInfo.pNext                            = nullptr;
            descriptorBufferBindingInfo.address                          = GetDeviceAddress(renderer.get(), &rayTraceDescriptorBuffer);
            descriptorBufferBindingInfo.usage                            = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

            fn_vkCmdBindDescriptorBuffersEXT(cmdBuf.CommandBuffer, 1, &descriptorBufferBindingInfo);

            uint32_t     bufferIndices           = 0;
            VkDeviceSize descriptorBufferOffsets = 0;
            fn_vkCmdSetDescriptorBufferOffsetsEXT(
                cmdBuf.CommandBuffer,
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                rayTracePipelineLayout.PipelineLayout,
                0, // firstSet
                1, // setCount
                &bufferIndices,
                &descriptorBufferOffsets);

            const uint32_t alignedHandleSize = Align(
                rayTracingProperties.shaderGroupHandleSize,
                rayTracingProperties.shaderGroupHandleAlignment);

            VkStridedDeviceAddressRegionKHR rgenShaderSBTEntry = {};
            rgenShaderSBTEntry.deviceAddress                   = GetDeviceAddress(renderer.get(), &rgenSBT);
            rgenShaderSBTEntry.stride                          = alignedHandleSize;
            rgenShaderSBTEntry.size                            = alignedHandleSize;

            VkStridedDeviceAddressRegionKHR missShaderSBTEntry = {};
            missShaderSBTEntry.deviceAddress                   = GetDeviceAddress(renderer.get(), &missSBT);
            missShaderSBTEntry.stride                          = alignedHandleSize;
            missShaderSBTEntry.size                            = alignedHandleSize;

            VkStridedDeviceAddressRegionKHR chitShaderSBTEntry = {};
            chitShaderSBTEntry.deviceAddress                   = GetDeviceAddress(renderer.get(), &hitgSBT);
            chitShaderSBTEntry.stride                          = alignedHandleSize;
            chitShaderSBTEntry.size                            = alignedHandleSize;

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

            CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

            // Execute command buffer
            CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

            // Wait for the GPU to finish the work
            if (!WaitForGpu(renderer.get())) {
                assert(false && "WaitForGpu failed");
            }
        }

        // Reset command buffer to render ImGui
        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        // ImGui
        {
            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                swapchainImages[swapchainImageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_COMPUTE_UNORDERED_ACCESS,
                RESOURCE_STATE_RENDER_TARGET);

            VkRenderPassAttachmentBeginInfo attachmentBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
            attachmentBeginInfo.pNext                           = 0;
            attachmentBeginInfo.attachmentCount                 = 1;
            attachmentBeginInfo.pAttachments                    = &swapchainImageViews[swapchainImageIndex];

            VkRect2D renderArea = {
                {0,            0            },
                {gWindowWidth, gWindowHeight}
            };

            VkRenderPassBeginInfo beginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            beginInfo.pNext                 = &attachmentBeginInfo;
            beginInfo.renderPass            = renderPass.RenderPass;
            beginInfo.framebuffer           = renderPass.Framebuffer;
            beginInfo.renderArea            = renderArea;

            vkCmdBeginRenderPass(cmdBuf.CommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &renderArea);

            // Draw ImGui
            window->ImGuiRenderDrawData(renderer.get(), cmdBuf.CommandBuffer);

            vkCmdEndRenderPass(cmdBuf.CommandBuffer);

            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                swapchainImages[swapchainImageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_RENDER_TARGET,
                RESOURCE_STATE_PRESENT);

            CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

            // Execute command buffer
            CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

            // Wait for the GPU to finish the work
            if (!WaitForGpu(renderer.get())) {
                assert(false && "WaitForGpu failed");
            }
        }

        // Update sample count
        if (sampleCount < gMaxSamples) {
            ++sampleCount;
        }

        if (!SwapchainPresent(renderer.get(), swapchainImageIndex)) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateRayTracePipelineLayout(
    VulkanRenderer*       pRenderer,
    VkSampler*            pImmutableSampler,
    VulkanPipelineLayout* pPipelineLayout)
{
    // Descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {};
        // Acceleration structure (t0)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 0;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            bindings.push_back(binding);
        }
        // Output texture (u1)
        // Accumulation texture (u2)
        // Ray generaiton sampling (u3)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 1;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            bindings.push_back(binding);

            binding                 = {};
            binding.binding         = 2;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            binding.descriptorCount = 1;
            binding.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            bindings.push_back(binding);

            binding                 = {};
            binding.binding         = 3;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            bindings.push_back(binding);
        }
        // Scene params (b5)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 5;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
            bindings.push_back(binding);
        }
        //  Index buffers (t20)
        //  Position buffers (t45)
        //  Normal buffers (t70)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 20;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount              = kMaxGeometries;
            binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            bindings.push_back(binding);

            binding                 = {};
            binding.binding         = 45;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = kMaxGeometries;
            binding.stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            bindings.push_back(binding);

            binding                 = {};
            binding.binding         = 70;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = kMaxGeometries;
            binding.stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            bindings.push_back(binding);
        }
        // Environment map (t100)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 100;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount              = kMaxIBLs;
            binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
            bindings.push_back(binding);
        }
        // Material params (t9)
        {
            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 9;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            bindings.push_back(binding);
        }

        // IBLMapSampler (s10)
        {
            VkSamplerCreateInfo createInfo     = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            createInfo.flags                   = 0;
            createInfo.magFilter               = VK_FILTER_LINEAR;
            createInfo.minFilter               = VK_FILTER_LINEAR;
            createInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            createInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            createInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            createInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            createInfo.mipLodBias              = 0;
            createInfo.anisotropyEnable        = VK_FALSE;
            createInfo.maxAnisotropy           = 0;
            createInfo.compareEnable           = VK_TRUE;
            createInfo.compareOp               = VK_COMPARE_OP_LESS_OR_EQUAL;
            createInfo.minLod                  = 0;
            createInfo.maxLod                  = FLT_MAX;
            createInfo.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            createInfo.unnormalizedCoordinates = VK_FALSE;

            CHECK_CALL(vkCreateSampler(
                pRenderer->Device,
                &createInfo,
                nullptr,
                pImmutableSampler));

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding                      = 10;
            binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount              = 1;
            binding.stageFlags                   = VK_SHADER_STAGE_ALL;
            binding.pImmutableSamplers           = pImmutableSampler;
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
            &pPipelineLayout->DescriptorSetLayout));
    }

    // Pipeline layout
    {
        VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        createInfo.setLayoutCount             = 1;
        createInfo.pSetLayouts                = &pPipelineLayout->DescriptorSetLayout;

        CHECK_CALL(vkCreatePipelineLayout(
            pRenderer->Device,
            &createInfo,
            nullptr,
            &pPipelineLayout->PipelineLayout));
    }
}

void CreateRayTracingPipeline(
    VulkanRenderer*             pRenderer,
    VkShaderModule              rayTraceModule,
    const VulkanPipelineLayout& pipelineLayout,
    VkPipeline*                 pPipeline)
{
    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {};
    // Ray gen
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        createInfo.module                          = rayTraceModule;
        createInfo.pName                           = gRayGenShaderName;

        shaderStages.push_back(createInfo);
    }
    // Miss
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_MISS_BIT_KHR;
        createInfo.module                          = rayTraceModule;
        createInfo.pName                           = gMissShaderName;

        shaderStages.push_back(createInfo);
    }
    // Closest hit
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        createInfo.module                          = rayTraceModule;
        createInfo.pName                           = gClosestHitShaderName;

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
    // Miss
    {
        VkRayTracingShaderGroupCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        createInfo.generalShader                        = 1; // shaderStages[1]
        createInfo.closestHitShader                     = VK_SHADER_UNUSED_KHR;
        createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
        createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

        shaderGroups.push_back(createInfo);
    }
    // Closest hit
    {
        VkRayTracingShaderGroupCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        createInfo.generalShader                        = VK_SHADER_UNUSED_KHR;
        createInfo.closestHitShader                     = 2; // shaderStages[2]
        createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
        createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

        shaderGroups.push_back(createInfo);
    }

    VkRayTracingPipelineInterfaceCreateInfoKHR pipelineInterfaceCreateInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR};
    //
    pipelineInterfaceCreateInfo.maxPipelineRayPayloadSize      = 4 * sizeof(float) + 3 * sizeof(uint32_t); // color, ray depth, sample count, ior;
    pipelineInterfaceCreateInfo.maxPipelineRayHitAttributeSize = 2 * sizeof(float);                        // barycentrics;

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.flags                             = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    createInfo.stageCount                        = CountU32(shaderStages);
    createInfo.pStages                           = DataPtr(shaderStages);
    createInfo.groupCount                        = CountU32(shaderGroups);
    createInfo.pGroups                           = DataPtr(shaderGroups);
    createInfo.maxPipelineRayRecursionDepth      = 16;
    createInfo.pLibraryInterface                 = &pipelineInterfaceCreateInfo;
    createInfo.layout                            = pipelineLayout.PipelineLayout;
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
    VulkanBuffer*                                    pRayGenSBT,
    VulkanBuffer*                                    pMissSBT,
    VulkanBuffer*                                    pHitGroupSBT)
{
    // Hardcoded group count
    const uint32_t groupCount = 3;

    // Handle sizes
    uint32_t groupHandleSize        = rayTracingProperties.shaderGroupHandleSize;
    uint32_t groupHandleAlignment   = rayTracingProperties.shaderGroupHandleAlignment;
    uint32_t alignedGroupHandleSize = Align(groupHandleSize, groupHandleAlignment);
    uint32_t totalGroupDataSize     = groupCount * groupHandleSize;

    //
    // This is what the shader group handles look like
    // in handlesData based on the pipeline. The offsets
    // are in bytes - assuming handleSize is 32 bytes.
    //
    //  +--------------+
    //  |  RGEN        | offset = 0
    //  +--------------+
    //  |  MISS        | offset = 32
    //  +--------------+
    //  |  HITG        | offset = 64
    //  +--------------+
    //
    std::vector<char> groupHandlesData(totalGroupDataSize);
    CHECK_CALL(fn_vkGetRayTracingShaderGroupHandlesKHR(
        pRenderer->Device,         // device
        pipeline,                  // pipeline
        0,                         // firstGroup
        groupCount,                // groupCount
        totalGroupDataSize,        // dataSize
        groupHandlesData.data())); // pData)

    // Usage flags for SBT buffer
    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;

    char* pShaderGroupHandleRGEN = groupHandlesData.data();
    char* pShaderGroupHandleMISS = groupHandlesData.data() + groupHandleSize;
    char* pShaderGroupHandleHITG = groupHandlesData.data() + 2 * groupHandleSize;

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
            groupHandleSize,          // srcSize
            pShaderGroupHandleRGEN,   // pSrcData
            usageFlags,               // usageFlags
            shaderGroupBaseAlignment, // minAlignment
            pRayGenSBT));             // pBuffer
    }
    // Miss
    {
        CHECK_CALL(CreateBuffer(
            pRenderer,                // pRenderer
            groupHandleSize,          // srcSize
            pShaderGroupHandleMISS,   // pSrcData
            usageFlags,               // usageFlags
            shaderGroupBaseAlignment, // minAlignment
            pMissSBT));               // pBuffer
    }
    // HITG: closest hit
    {
        CHECK_CALL(CreateBuffer(
            pRenderer,                // pRenderer
            groupHandleSize,          // srcSize
            pShaderGroupHandleHITG,   // pSrcData
            usageFlags,               // usageFlags
            shaderGroupBaseAlignment, // minAlignment
            pHitGroupSBT));           // pBuffer
    }
}

void CreateGeometries(
    VulkanRenderer* pRenderer,
    Geometry&       outSphereGeometry,
    Geometry&       outKnobGeometry,
    Geometry&       outMonkeyGeometry,
    Geometry&       outTeapotGeometry,
    Geometry&       outBoxGeometryy)
{
    VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

    // Sphere
    {
        TriMesh mesh = TriMesh::Sphere(1.0f, 256, 256, {.enableNormals = true});

        Geometry& geo = outSphereGeometry;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            usageFlags,
            0,
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            usageFlags,
            0,
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            usageFlags,
            0,
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();
    }

    // Knob
    {
        TriMesh::Options options  = {.enableNormals = true};
        options.applyTransform    = true;
        options.transformRotate.y = glm::radians(180.0f);

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.25f);

        Geometry& geo = outKnobGeometry;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            usageFlags,
            0,
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            usageFlags,
            0,
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            usageFlags,
            0,
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();
    }

    // Monkey
    {
        TriMesh::Options options = {.enableNormals = true};

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/monkey_lowres.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.20f);

        Geometry& geo = outMonkeyGeometry;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            usageFlags,
            0,
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            usageFlags,
            0,
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            usageFlags,
            0,
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();
    }

    // Teapot
    {
        TriMesh::Options options  = {.enableNormals = true};
        options.applyTransform    = true;
        options.transformRotate.y = glm::radians(160.0f);

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/teapot.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.5f);

        Geometry& geo = outTeapotGeometry;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            usageFlags,
            0,
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            usageFlags,
            0,
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            usageFlags,
            0,
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();
    }

    // Box
    {
        TriMesh::Options options = {.enableNormals = true};

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/shelf.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }

        Geometry& geo = outBoxGeometryy;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            usageFlags,
            0,
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            usageFlags,
            0,
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            usageFlags,
            0,
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();
    }
}

void CreateBLASes(
    VulkanRenderer*    pRenderer,
    const Geometry&    sphereGeometry,
    const Geometry&    knobGeometry,
    const Geometry&    monkeyGeometry,
    const Geometry&    teapotGeometry,
    const Geometry&    boxGeometry,
    VulkanAccelStruct* pSphereBLAS,
    VulkanAccelStruct* pKnobBLAS,
    VulkanAccelStruct* pMonkeyBLAS,
    VulkanAccelStruct* pTeapotBLAS,
    VulkanAccelStruct* pBoxBLAS)
{
    std::vector<const Geometry*>    geometries = {&sphereGeometry, &knobGeometry, &monkeyGeometry, &teapotGeometry, &boxGeometry};
    std::vector<VulkanAccelStruct*> BLASes     = {pSphereBLAS, pKnobBLAS, pMonkeyBLAS, pTeapotBLAS, pBoxBLAS};

    // clang-format off
	VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};
    // clang-format on

    VulkanBuffer transformBuffer;
    {
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

        CHECK_CALL(CreateBuffer(
            pRenderer,               // pRenderer
            sizeof(transformMatrix), // srcSize
            &transformMatrix,        // pSrcData
            usageFlags,              // usageFlags
            0,                       // minAlignment
            &transformBuffer));      // pBuffer
    }

    uint32_t n = static_cast<uint32_t>(geometries.size());
    for (uint32_t i = 0; i < n; ++i) {
        auto pGeometry = geometries[i];
        auto pBLAS     = BLASes[i];

        VkAccelerationStructureGeometryKHR geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        //
        geometry.flags                                          = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType                                   = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat                = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress    = GetDeviceAddress(pRenderer, &pGeometry->positionBuffer);
        geometry.geometry.triangles.vertexStride                = 12;
        geometry.geometry.triangles.maxVertex                   = pGeometry->vertexCount;
        geometry.geometry.triangles.indexType                   = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress     = GetDeviceAddress(pRenderer, &pGeometry->indexBuffer);
        geometry.geometry.triangles.transformData.deviceAddress = GetDeviceAddress(pRenderer, &transformBuffer);

        VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        //
        buildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildGeometryInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeometryInfo.geometryCount = 1;
        buildGeometryInfo.pGeometries   = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        const uint32_t                           numTriangles   = pGeometry->indexCount / 3;
        fn_vkGetAccelerationStructureBuildSizesKHR(
            pRenderer->Device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeometryInfo,
            &numTriangles,
            &buildSizesInfo);

        // Scratch buffer
        VulkanBuffer scratchBuffer = {};
        {
            // Get acceleration structure properties
            VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};

            VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext                       = &accelStructProperties;
            vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

            VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

            CHECK_CALL(CreateBuffer(
                pRenderer,                                                            // pRenderer
                buildSizesInfo.buildScratchSize,                                      // srcSize
                usageFlags,                                                           // usageFlags
                VMA_MEMORY_USAGE_GPU_ONLY,                                            // memoryUsage
                accelStructProperties.minAccelerationStructureScratchOffsetAlignment, // minAlignment
                &scratchBuffer));                                                     // pBuffer
        }

        // Create acceleration structure buffer
        {
            VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;

            CHECK_CALL(CreateBuffer(
                pRenderer,
                buildSizesInfo.accelerationStructureSize,
                usageFlags,
                VMA_MEMORY_USAGE_GPU_ONLY,
                0,
                &pBLAS->Buffer));
        }

        // Create accleration structure object
        {
            VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            createInfo.buffer                               = pBLAS->Buffer.Buffer;
            createInfo.offset                               = 0;
            createInfo.size                                 = buildSizesInfo.accelerationStructureSize;
            createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

            CHECK_CALL(fn_vkCreateAccelerationStructureKHR(pRenderer->Device, &createInfo, nullptr, &pBLAS->AccelStruct));
        }

        // Build acceleration structure
        //
        {
            // Build geometry info
            VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            //
            buildGeometryInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildGeometryInfo.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildGeometryInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildGeometryInfo.dstAccelerationStructure  = pBLAS->AccelStruct;
            buildGeometryInfo.geometryCount             = 1;
            buildGeometryInfo.pGeometries               = &geometry;
            buildGeometryInfo.scratchData.deviceAddress = GetDeviceAddress(pRenderer, &scratchBuffer);

            // Build range info
            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {};
            buildRangeInfo.primitiveCount                           = numTriangles;

            CommandObjects cmdBuf = {};
            CHECK_CALL(CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf));

            VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

            const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
            fn_vkCmdBuildAccelerationStructuresKHR(cmdBuf.CommandBuffer, 1, &buildGeometryInfo, &pBuildRangeInfo);

            CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

            CHECK_CALL(ExecuteCommandBuffer(pRenderer, &cmdBuf));

            if (!WaitForGpu(pRenderer)) {
                assert(false && "WaitForGpu failed");
            }
        }

        DestroyBuffer(pRenderer, &scratchBuffer);
    }

    DestroyBuffer(pRenderer, &transformBuffer);
}

void CreateTLAS(
    VulkanRenderer*                  pRenderer,
    const VulkanAccelStruct&         sphereBLAS,
    const VulkanAccelStruct&         knobBLAS,
    const VulkanAccelStruct&         monkeyBLAS,
    const VulkanAccelStruct&         teapotBLAS,
    const VulkanAccelStruct&         boxBLAS,
    VulkanAccelStruct*               pTLAS,
    std::vector<MaterialParameters>& outMaterialParams)
{
    // clang-format off
     std::vector<glm::mat3x4> transforms = {
         // Rough plastic sphere
         {{ 1.0f, 0.0f, 0.0f, 1.25f},
          { 0.0f, 1.0f, 0.0f, 4.0f},
          { 0.0f, 0.0f, 1.0f, 1.5f}},
         // Shiny plastic sphere
         {{-1.0f, 0.0f,  0.0f, -1.25f},
          { 0.0f, 1.0f,  0.0f,  1.0f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Crystal sphere
         {{1.0f, 0.0f, 0.0f,  3.75f},
          {0.0f, 1.0f, 0.0f,  1.0f},
          {0.0f, 0.0f, 1.0f,  1.5f}},
         // Metal sphere
         {{-1.0f, 0.0f,  0.0f,  3.75f},
          { 0.0f, 1.0f,  0.0f,  4.0f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},

         // Rough plastic knob
         {{-1.0f, 0.0f,  0.0f,  3.75f},
          { 0.0f, 1.0f,  0.0f,  0.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Shiny plastic knob
         {{-1.0f, 0.0f,  0.0f, -3.75f},
          { 0.0f, 1.0f,  0.0f,  3.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Glass knob
         {{1.0f, 0.0f, 0.0f, -3.75f},
          {0.0f, 1.0f, 0.0f,  3.96f},
          {0.0f, 0.0f, 1.0f,  1.5f}},
         // Metal knob
         {{1.0f, 0.0f, 0.0f, -1.25f},
          {0.0f, 1.0f, 0.0f,  0.96f},
          {0.0f, 0.0f, 1.0f,  1.5f}},

         // Rough plastic monkey
         {{-1.0f, 0.0f,  0.0f,  1.25f},
          { 0.0f, 1.0f,  0.0f,  3.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Shiny plastic monkey
         {{1.0f, 0.0f, 0.0f,  1.25f},
          {0.0f, 1.0f, 0.0f,  0.96f},
          {0.0f, 0.0f, 1.0f,  1.5f}},
         // Diamond monkey
         {{-1.0f, 0.0f,  0.0f, -3.75f},
          { 0.0f, 1.0f,  0.0f,  0.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Metal monkey
         {{ 1.0f, 0.0f,  0.0f,  3.75f},
          { 0.0f, 1.0f,  0.0f,  3.96f},
          { 0.0f, 0.0f,  1.0f,  1.5f}},

         // Rough plastic teapot
         {{ 1.0f, 0.0f,  0.0f, -3.75f},
          { 0.0f, 1.0f,  0.0f,  0.001f},
          { 0.0f, 0.0f,  1.0f,  1.35f}},
         // Shiny plastic teapot
         {{1.0f, 0.0f, 0.0f, -1.25f},
          {0.0f, 1.0f, 0.0f,  3.001f},
          {0.0f, 0.0f, 1.0f,  1.35f}},
         // Glass teapot
         {{-1.0f, 0.0f,  0.0f, -1.25f},
          { 0.0f, 1.0f,  0.0f,  3.001f},
          { 0.0f, 0.0f, -1.0f, -1.35f}},
         // Metal teapot
         {{-1.0f, 0.0f,  0.0f,  1.25f},
          { 0.0f, 1.0f,  0.0f,  0.001f},
          { 0.0f, 0.0f, -1.0f, -1.35f}},

         // Box
         {{1.0f, 0.0f, 0.0f,  0.0f},
          {0.0f, 1.0f, 0.0f,  0.0f},
          {0.0f, 0.0f, 1.0f,  0.0f}},
     };
    // clang-format on

    // Material params
    {
        // ---------------------------------------------------------------------
        // Spheres
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.0f, 1.0f, 1.0f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.07f, 0.05f, 0.1f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Crystal
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_DiletricCrystal;
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 2.0f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalChromium;
            materialParams.roughness           = 0.25f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Knob
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.0f, 0.0f, 1.0f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.25f, 0.07f, 0.05f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Glass
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 1.5f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalGold;
            materialParams.roughness           = 0.25f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Monkey
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.0f, 1.0f, 0.2f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.2f, 1.0f, 0.2f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Diamond
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_DiletricDiamond + vec3(0, 0, 0.25f);
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 2.418f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalSilver;
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Teapot
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.0f, 1.0f, 1.0f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;
            materialParams.emissionColor       = vec3(1.0f, 1.0f, 1.0f);

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = 2.0f * vec3(1.0f, 0.35f, 0.05f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Glass
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 0.25f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 1.5f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalCopper;
            materialParams.roughness           = 0.45f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Box
        // ---------------------------------------------------------------------

        // Box
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.35f, 0.36f, 0.36f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.2f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }
    }

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    {
        VkAccelerationStructureInstanceKHR instance = {};
        instance.mask                               = 0xFF;
        instance.flags                              = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;

        uint32_t transformIdx = 0;

        // ---------------------------------------------------------------------
        // Sphere
        // ---------------------------------------------------------------------
        instance.accelerationStructureReference = GetDeviceAddress(pRenderer, sphereBLAS.AccelStruct);

        // Rough plastic sphere
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Shiny plastic sphere
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Crystal sphere
        VkAccelerationStructureInstanceKHR thisInstance = instance;
        thisInstance.flags                              = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
        memcpy(&thisInstance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(thisInstance);
        ++transformIdx;

        // Metal sphere
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Knob
        // ---------------------------------------------------------------------
        instance.accelerationStructureReference = GetDeviceAddress(pRenderer, knobBLAS.AccelStruct);

        // Rough plastic knob
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Shiny plastic knob
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Glass knob
        thisInstance       = instance;
        thisInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
        memcpy(&thisInstance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(thisInstance);
        ++transformIdx;

        // Metal knob
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Monkey
        // ---------------------------------------------------------------------
        instance.accelerationStructureReference = GetDeviceAddress(pRenderer, monkeyBLAS.AccelStruct);

        // Rough plastic monkey
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Shiny plastic monkey
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Diamond monkey
        thisInstance       = instance;
        thisInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
        memcpy(&thisInstance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(thisInstance);
        ++transformIdx;

        // Metal monkey
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Teapot
        // ---------------------------------------------------------------------
        instance.accelerationStructureReference = GetDeviceAddress(pRenderer, teapotBLAS.AccelStruct);

        // Rough plastic teapot
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Shiny plastic teapot
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // Glass teapot
        thisInstance       = instance;
        thisInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
        memcpy(&thisInstance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(thisInstance);
        ++transformIdx;

        // Metal teapot
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Box
        // ---------------------------------------------------------------------
        instance.accelerationStructureReference = GetDeviceAddress(pRenderer, boxBLAS.AccelStruct);
        memcpy(&instance.transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instances.push_back(instance);
        ++transformIdx;
    }

    VulkanBuffer instanceBuffer = {};
    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(instances),
        DataPtr(instances),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        0,
        &instanceBuffer));

    // Geometry
    VkAccelerationStructureGeometryKHR geometry    = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = GetDeviceAddress(pRenderer, &instanceBuffer);

    // Build geometry info - fill out enough to get build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    //
    buildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeometryInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildGeometryInfo.geometryCount = 1;
    buildGeometryInfo.pGeometries   = &geometry;

    // Get acceleration structure build size
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    const uint32_t                           numInstances   = CountU32(instances);
    fn_vkGetAccelerationStructureBuildSizesKHR(
        pRenderer->Device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildGeometryInfo,
        &numInstances,
        &buildSizesInfo);

    // Create scratch buffer
    VulkanBuffer scratchBuffer = {};
    {
        // Get acceleration structure properties
        //
        // Obviously this can be cached if it's accessed frequently.
        //
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};

        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &accelStructProperties;
        vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        CHECK_CALL(CreateBuffer(
            pRenderer,                                                            // pRenderer
            buildSizesInfo.buildScratchSize,                                      // srcSize
            usageFlags,                                                           // usageFlags
            VMA_MEMORY_USAGE_GPU_ONLY,                                            // memoryUsage
            accelStructProperties.minAccelerationStructureScratchOffsetAlignment, // minAlignment
            &scratchBuffer));                                                     // pBuffer
    }

    // Create acceleration structure buffer
    {
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            buildSizesInfo.accelerationStructureSize,
            usageFlags,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0,
            &pTLAS->Buffer));
    }

    // Create accleration structure object
    {
        VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer                               = pTLAS->Buffer.Buffer;
        createInfo.offset                               = 0;
        createInfo.size                                 = buildSizesInfo.accelerationStructureSize;
        createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        CHECK_CALL(fn_vkCreateAccelerationStructureKHR(pRenderer->Device, &createInfo, nullptr, &pTLAS->AccelStruct));
    }

    // Build acceleration structure
    //
    {
        // Build geometry info - update this for build
        buildGeometryInfo.dstAccelerationStructure  = pTLAS->AccelStruct;
        buildGeometryInfo.scratchData.deviceAddress = GetDeviceAddress(pRenderer, &scratchBuffer);

        // Build range info
        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {};
        buildRangeInfo.primitiveCount                           = numInstances;

        CommandObjects cmdBuf = {};
        CHECK_CALL(CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf));

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
        fn_vkCmdBuildAccelerationStructuresKHR(cmdBuf.CommandBuffer, 1, &buildGeometryInfo, &pBuildRangeInfo);

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        CHECK_CALL(ExecuteCommandBuffer(pRenderer, &cmdBuf));

        if (!WaitForGpu(pRenderer)) {
            assert(false && "WaitForGpu failed");
        }
    }

    DestroyBuffer(pRenderer, &instanceBuffer);
    DestroyBuffer(pRenderer, &scratchBuffer);
}

void CreateAccumTexture(VulkanRenderer* pRenderer, VulkanImage* pBuffer)
{
    CHECK_CALL(CreateImage(
        pRenderer,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT,
        gWindowWidth,
        gWindowHeight,
        1,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        1,
        1,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VMA_MEMORY_USAGE_GPU_ONLY,
        pBuffer));

    CHECK_CALL(TransitionImageLayout(
        pRenderer,
        pBuffer->Image,
        GREX_ALL_SUBRESOURCES,
        VK_IMAGE_ASPECT_COLOR_BIT,
        RESOURCE_STATE_UNKNOWN,
        RESOURCE_STATE_COMMON));
}

void CreateIBLTextures(
    VulkanRenderer*           pRenderer,
    std::vector<IBLTextures>& outIBLTextures)
{
    std::vector<std::filesystem::path> iblFiles;
    {
        auto iblDirs = GetEveryAssetPath("IBL");
        for (auto& dir : iblDirs) {
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                auto path = entry.path();
                auto ext  = path.extension();
                if (ext == ".ibl") {
                    path = std::filesystem::relative(path, dir.parent_path());
                    iblFiles.push_back(path);
                }
            }
        }
    }

    size_t maxEntries = std::min<size_t>(kMaxIBLs, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i) {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl)) {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            return;
        }

        IBLTextures iblTexture = {};

        iblTexture.envNumLevels = ibl.numLevels;

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

            VulkanImage texture;
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.baseWidth,
                ibl.baseHeight,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                mipOffsets,
                ibl.environmentMap.GetSizeInBytes(),
                ibl.environmentMap.GetPixels(),
                &texture));
            iblTexture.envTexture = texture;

            outIBLTextures.push_back(iblTexture);
        }

        gIBLNames.push_back(iblFile.filename().replace_extension().string());

        GREX_LOG_INFO("Loaded " << iblFile);
    }
}

void CreateDescriptorBuffer(
    VulkanRenderer*       pRenderer,
    VkDescriptorSetLayout descriptorSetLayout,
    VulkanBuffer*         pBuffer)
{
    VkDeviceSize size = 0;
    fn_vkGetDescriptorSetLayoutSizeEXT(pRenderer->Device, descriptorSetLayout, &size);

    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    CHECK_CALL(CreateBuffer(
        pRenderer,  // pRenderer
        size,       // srcSize
        nullptr,    // pSrcData
        usageFlags, // usageFlags
        0,          // minAlignment
        pBuffer));  // pBuffer
}

void WriteDescriptors(
    VulkanRenderer*                 pRenderer,
    VkDescriptorSetLayout           descriptorSetLayout,
    VulkanBuffer*                   pDescriptorBuffer,
    const VulkanBuffer&             sceneParamsBuffer,
    const VulkanAccelStruct&        accelStruct,
    const VulkanImage&              accumTexture,
    const VulkanBuffer&             rayGenSamplesBuffer,
    const Geometry&                 sphereGeometry,
    const Geometry&                 knobGeometry,
    const Geometry&                 monkeyGeometry,
    const Geometry&                 teapotGeometry,
    const Geometry&                 boxGeometry,
    const VulkanBuffer&             materialParamsBuffer,
    const std::vector<IBLTextures>& iblTextures,
    VkImageView*                    pAccumImageView,
    std::vector<VkImageView>*       pIBLImageViews)
{
    char* pDescriptorBufferStartAddress = nullptr;
    CHECK_CALL(vmaMapMemory(
        pRenderer->Allocator,
        pDescriptorBuffer->Allocation,
        reinterpret_cast<void**>(&pDescriptorBufferStartAddress)));

    // Scene params (b5)
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        5, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        &sceneParamsBuffer);

    // Acceleration strcutured (t0)
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        0, // binding,
        0, // arrayElement,
        &accelStruct);

    //
    // NOTE: Ouput texture (u1) will be updated per frame
    //

    // Accumulation texture (u2)
    {
        VkImageView imageView = VK_NULL_HANDLE;
        CHECK_CALL(CreateImageView(
            pRenderer,
            &accumTexture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            GREX_ALL_SUBRESOURCES,
            &imageView));
        *pAccumImageView = imageView;

        WriteDescriptor(
            pRenderer,
            pDescriptorBufferStartAddress,
            descriptorSetLayout,
            2, // binding
            0, // arrayElement
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            imageView,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    // Ray generation samples (u3)
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        3, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        &rayGenSamplesBuffer);

    // Geometry
    {
        const uint32_t kNumInstances          = 4;
        const uint32_t kIndexBufferBinding    = 20; // Index buffer (t20)
        const uint32_t kPositionBufferBinding = 45; // Position buffer (t45)
        const uint32_t kNormalBufferBinding   = 70; // Normal buffer (t70)

        uint32_t arrayElement = 0;

        // Spheres
        for (uint32_t i = 0; i < kNumInstances; ++i, ++arrayElement) {
            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kIndexBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &sphereGeometry.indexBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kPositionBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &sphereGeometry.positionBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kNormalBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &sphereGeometry.normalBuffer);
        }

        // Knob
        for (uint32_t i = 0; i < kNumInstances; ++i, ++arrayElement) {
            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kIndexBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &knobGeometry.indexBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kPositionBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &knobGeometry.positionBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kNormalBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &knobGeometry.normalBuffer);
        }

        // Monkey
        for (uint32_t i = 0; i < kNumInstances; ++i, ++arrayElement) {
            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kIndexBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &monkeyGeometry.indexBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kPositionBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &monkeyGeometry.positionBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kNormalBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &monkeyGeometry.normalBuffer);
        }

        // Teapot
        for (uint32_t i = 0; i < kNumInstances; ++i, ++arrayElement) {
            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kIndexBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &teapotGeometry.indexBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kPositionBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &teapotGeometry.positionBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kNormalBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &teapotGeometry.normalBuffer);
        }

        // Box
        uint32_t instanceStride = 0 * kNumInstances;
        {
            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kIndexBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &boxGeometry.indexBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kPositionBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &boxGeometry.positionBuffer);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                kNormalBufferBinding,
                arrayElement,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &boxGeometry.normalBuffer);
        }
    }

    // Material params (t9)
    WriteDescriptor(
        pRenderer,
        pDescriptorBufferStartAddress,
        descriptorSetLayout,
        9, // binding
        0, // arrayElement
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        &materialParamsBuffer);

    // IBL environment textures (t100)
    {
        uint32_t arrayElement = 0;
        for (uint32_t i = 0; i < iblTextures.size(); ++i, ++arrayElement) {
            auto& iblTexture = iblTextures[i];

            VkImageView imageView = VK_NULL_HANDLE;
            CHECK_CALL(CreateImageView(
                pRenderer,
                &iblTexture.envTexture,
                VK_IMAGE_VIEW_TYPE_2D,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                0,
                iblTexture.envNumLevels,
                0,
                1,
                &imageView));
            pIBLImageViews->push_back(imageView);

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                descriptorSetLayout,
                100,
                arrayElement,
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                imageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    vmaUnmapMemory(pRenderer->Allocator, pDescriptorBuffer->Allocation);
}
