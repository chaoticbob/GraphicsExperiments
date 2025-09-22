#include "window.h"

#include "vk_renderer.h"

#include "sphereflake.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
using glm::vec3;

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

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0) uniform CameraProperties 
{
	mat4 viewInverse;
	mat4 projInverse;
} cam;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main() 
{
	const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
	vec2 d = inUV * 2.0 - 1.0;
    d.y = -d.y;

	vec4 origin = cam.viewInverse * vec4(0, 0, 0, 1);
	vec4 target = cam.projInverse * vec4(d.x, d.y, 1, 1);
	vec4 direction = normalize(cam.viewInverse * vec4(normalize(target.xyz), 0));

	float tmin = 0.001;
	float tmax = 10000.0;

    hitValue = vec3(0.0);

    traceRayEXT(
        topLevelAS,           // topLevel
        gl_RayFlagsOpaqueEXT, // rayFlags
        0xff,                 // cullMask
        0,                    // sbtRecordOffset
        0,                    // sbtRecordStride
        0,                    // missIndex
        origin.xyz,           // origin
        tmin,                 // Tmin
        direction.xyz,        // direction
        tmax,                 // Tmax
        0);                   // payload

	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
}

)";

const char* gShaderMISS = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
    hitValue = vec3(0, 0, 0);
}
)";

const char* gShaderCHIT = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

hitAttributeEXT vec3 hitNormal;

void main()
{
    vec3 hitPosition = gl_WorldRayOriginEXT + gl_RayTmaxEXT * gl_WorldRayDirectionEXT;

    // Lambert shading
    vec3 lightPos = vec3(2, 5, 5);
    vec3 lightDir = normalize(lightPos - hitPosition);
    float d = 0.8 * clamp(dot(lightDir, hitNormal), 0, 1);
    float a = 0.2;

    hitValue = vec3(clamp(a + d, 0, 1));
}
)";

const char* gShaderRINT = R"(
//
// Based on:
//   https://github.com/georgeouzou/vk_exp/blob/master/shaders/sphere.rint
//
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Sphere {
    float minX; 
    float minY;
    float minZ;
    float maxX; 
    float maxY;
    float maxZ;
};

layout(buffer_reference, scalar, buffer_reference_align = 8) buffer SphereBuffer
{
	Sphere spheres[];
};

layout(shaderRecordEXT, std430) buffer ShaderRecord
{
	SphereBuffer sphereBuffer;
};

hitAttributeEXT vec3 hitNormal;

// this method is documented in raytracing gems book
vec2 gems_intersections(vec3 orig, vec3 dir, vec3 center, float radius)
{
	vec3  f = orig - center;
	float a = dot(dir, dir);
	float bi = dot(-f, dir);
	float c = dot(f, f) - radius * radius;
	vec3  s = f + (bi/a)*dir;
	float discr = radius * radius - dot(s, s);

	vec2 t = vec2(-1.0, -1.0);
	if (discr >= 0) {
		float q = bi + sign(bi) * sqrt(a*discr);
		float t1 = c / q;
		float t2 = q / a;
		t = vec2(t1, t2);
	}
	return t;
}

void main()
{   
	vec3 orig = gl_WorldRayOriginEXT;
	vec3 dir = gl_WorldRayDirectionEXT;

    Sphere sphere = sphereBuffer.spheres[gl_PrimitiveID];

	vec3 aabb_min = vec3(sphere.minX, sphere.minY, sphere.minZ);
	vec3 aabb_max = vec3(sphere.maxX, sphere.maxY, sphere.maxZ);

	vec3 center = (aabb_max + aabb_min) / vec3(2.0);
	float radius = (aabb_max.x - aabb_min.x) / 2.0;

    // Might be some wonky behavior if inside sphere
	vec2 t = gems_intersections(orig, dir, center, radius);

    if (t.x > 0) {
	    hitNormal = normalize((orig + t.x * dir) - center);
	    reportIntersectionEXT(t.x, 0);
    }
    
    if (t.y > 0) {
	    hitNormal = normalize((orig + t.y * dir) - center);
	    reportIntersectionEXT(t.y, 0);
    }
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth        = 1280;
static uint32_t gWindowHeight       = 720;
static bool     gEnableDebug        = true;
static uint32_t gUniformmBufferSize = 256;

void CreateSphereBuffer(VulkanRenderer* pRenderer, uint32_t* pNumSpheres, VulkanBuffer* pBuffer);
void CreateDescriptorSetLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout* pLayout);
void CreatePipelineLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout* pLayout);
void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvRGEN,
    const std::vector<uint32_t>& spirvMISS,
    const std::vector<uint32_t>& spirvCHIT,
    const std::vector<uint32_t>& spirvRINT,
    VkShaderModule*              pModuleRGEN,
    VkShaderModule*              pModuleMISS,
    VkShaderModule*              pModuleCHIT,
    VkShaderModule*              pModuleRINT);
void CreateRayTracingPipeline(
    VulkanRenderer*  pRenderer,
    VkShaderModule   moduleRGEN,
    VkShaderModule   moduleMISS,
    VkShaderModule   moduleCHIT,
    VkShaderModule   moduleRINT,
    VkPipelineLayout pipelineLayout,
    VkPipeline*      pPipeline);
void CreateShaderBindingTables(
    VulkanRenderer*                                  pRenderer,
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
    VkPipeline                                       pipeline,
    const VulkanBuffer*                              pSphereBuffer,
    VulkanBuffer*                                    pRayGenSBT,
    VulkanBuffer*                                    pMissSBT,
    VulkanBuffer*                                    pHitGroupSBT);
void CreateBLAS(
    VulkanRenderer*     pRenderer,
    uint32_t            numSpheres,
    const VulkanBuffer* pSphereBuffer,
    VulkanAccelStruct*  pBLAS);
void CreateTLAS(VulkanRenderer* pRenderer, const VulkanAccelStruct* pBLAS, VulkanAccelStruct* pTLAS);
void CreateUniformBuffer(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer);
void CreateDescriptors(
    VulkanRenderer*      pRenderer,
    VulkanDescriptorSet* pDescriptors,
    VulkanAccelStruct*   pTLAS,
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
    std::vector<uint32_t> spirvMISS;
    std::vector<uint32_t> spirvCHIT;
    std::vector<uint32_t> spirvRINT;
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

        res = CompileGLSL(gShaderMISS, VK_SHADER_STAGE_MISS_BIT_KHR, {}, &spirvMISS, &errorMsg);
        if (res != COMPILE_SUCCESS)
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (MISS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        res = CompileGLSL(gShaderCHIT, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, {}, &spirvCHIT, &errorMsg);
        if (res != COMPILE_SUCCESS)
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (CHIT): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        res = CompileGLSL(gShaderRINT, VK_SHADER_STAGE_INTERSECTION_BIT_KHR, {}, &spirvRINT, &errorMsg);
        if (res != COMPILE_SUCCESS)
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (RINT): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Sphere buffer
    // *************************************************************************
    uint32_t     numSpheres   = 0;
    VulkanBuffer sphereBuffer = {};
    CreateSphereBuffer(renderer.get(), &numSpheres, &sphereBuffer);

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
    VkShaderModule moduleRINT = VK_NULL_HANDLE;
    CreateShaderModules(
        renderer.get(),
        spirvRGEN,
        spirvMISS,
        spirvCHIT,
        spirvRINT,
        &moduleRGEN,
        &moduleMISS,
        &moduleCHIT,
        &moduleRINT);

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
    // The pipeline is created with 3 shader groups:
    //    1) Ray gen
    //    2) Miss
    //    3) Hitgroup
    //
    // *************************************************************************
    VkPipeline pipeline = VK_NULL_HANDLE;
    CreateRayTracingPipeline(
        renderer.get(),
        moduleRGEN,
        moduleMISS,
        moduleCHIT,
        moduleRINT,
        pipelineLayout,
        &pipeline);

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
        pipeline,
        &sphereBuffer,
        &rgenSBT,
        &missSBT,
        &hitgSBT);

    // *************************************************************************
    // Bottom level acceleration structure
    // *************************************************************************
    VulkanAccelStruct blas;
    CreateBLAS(renderer.get(), numSpheres, &sphereBuffer, &blas);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    VulkanAccelStruct tlas;
    CreateTLAS(renderer.get(), &blas, &tlas);

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
            &tlas,
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
            missShaderSBTEntry.deviceAddress                   = GetDeviceAddress(renderer.get(), &missSBT);
            missShaderSBTEntry.stride                          = alignedHandleSize;
            missShaderSBTEntry.size                            = alignedHandleSize;

            VkStridedDeviceAddressRegionKHR hitgShaderSBTEntry = {};
            hitgShaderSBTEntry.deviceAddress                   = GetDeviceAddress(renderer.get(), &hitgSBT);
            hitgShaderSBTEntry.stride                          = Align(alignedHandleSize + 8, rayTracingProperties.shaderGroupHandleSize);
            hitgShaderSBTEntry.size                            = Align(alignedHandleSize + 8, rayTracingProperties.shaderGroupHandleSize);

            VkStridedDeviceAddressRegionKHR callableShaderSbtEntry = {};

            fn_vkCmdTraceRaysKHR(
                cmdBuf.CommandBuffer,
                &rgenShaderSBTEntry,
                &missShaderSBTEntry,
                &hitgShaderSBTEntry,
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

void CreateSphereBuffer(VulkanRenderer* pRenderer, uint32_t* pNumSpheres, VulkanBuffer* pBuffer)
{
    std::vector<SphereFlake> spheres;

    SphereFlake sphere = {};

    // Ground plane sphere
    float groundSize = 1000.0f;
    sphere.aabbMin   = (groundSize * vec3(-1, -1, -1)) - vec3(0, groundSize, 0);
    sphere.aabbMax   = (groundSize * vec3(1, 1, 1)) - vec3(0, groundSize, 0);
    spheres.push_back(sphere);

    // Initial sphere
    float radius   = 1;
    sphere.aabbMin = (radius * vec3(-1, -1, -1)) + vec3(0, radius, 0);
    sphere.aabbMax = (radius * vec3(1, 1, 1)) + vec3(0, radius, 0);
    spheres.push_back(sphere);

    GenerateSphereFlake(0, 4, radius / 3.0f, radius, vec3(0, radius, 0), vec3(0, 1, 0), spheres);

    *pNumSpheres = CountU32(spheres);

    VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

    CHECK_CALL(CreateBuffer(
        pRenderer,            // pRenderer
        SizeInBytes(spheres), // srcSize
        DataPtr(spheres),     // pSrcData
        usageFlags,           // usageFlags
        8,                    // minAlignment
        pBuffer));            // pBuffer
}

void CreateDescriptorSetLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout* pLayout)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    // layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 0;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings.push_back(binding);
    }
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
    const std::vector<uint32_t>& spirvMISS,
    const std::vector<uint32_t>& spirvCHIT,
    const std::vector<uint32_t>& spirvRINT,
    VkShaderModule*              pModuleRGEN,
    VkShaderModule*              pModuleMISS,
    VkShaderModule*              pModuleCHIT,
    VkShaderModule*              pModuleRINT)
{
    // Ray gen
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvRGEN);
        createInfo.pCode                    = DataPtr(spirvRGEN);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleRGEN));
    }

    // Miss
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvMISS);
        createInfo.pCode                    = DataPtr(spirvMISS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleMISS));
    }

    // Closeset hit
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvCHIT);
        createInfo.pCode                    = DataPtr(spirvCHIT);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleCHIT));
    }

    // Intersection
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvRINT);
        createInfo.pCode                    = DataPtr(spirvRINT);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleRINT));
    }
}

void CreateRayTracingPipeline(
    VulkanRenderer*  pRenderer,
    VkShaderModule   moduleRGEN,
    VkShaderModule   moduleMISS,
    VkShaderModule   moduleCHIT,
    VkShaderModule   moduleRINT,
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
    // Miss
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_MISS_BIT_KHR;
        createInfo.module                          = moduleMISS;
        createInfo.pName                           = "main";

        shaderStages.push_back(createInfo);
    }
    // Closest hit
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        createInfo.module                          = moduleCHIT;
        createInfo.pName                           = "main";

        shaderStages.push_back(createInfo);
    }
    // Intersection
    {
        VkPipelineShaderStageCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        createInfo.stage                           = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        createInfo.module                          = moduleRINT;
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
    // Closest hit + Intersection
    {
        VkRayTracingShaderGroupCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        createInfo.generalShader                        = VK_SHADER_UNUSED_KHR;
        createInfo.closestHitShader                     = 2; // shaderStages[2]
        createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
        createInfo.intersectionShader                   = 3; // shaderStages[3]

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
    const VulkanBuffer*                              pSphereBuffer,
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
    // are in bytes - assuming alignedHandleSize is 32 bytes.
    //
    //  +--------+
    //  |  RGEN  | offset = 0
    //  +--------+
    //  |  MISS  | offset = 32
    //  +--------+
    //  |  HITG  | offset = 64
    //  +--------+
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
    // HITG: closest hit + intersection
    {
        //
        // This hit group's shader record size is 64 since we need space after
        // the group handle to store the virtual address for the sphere buffer.
        //
        // NOTE: A single identifier is used for all the shaders in the hit group.
        //       This is why there is not separate shader records for the closest hit
        //       shader and the intersection shader.
        //

        // 8 bytes for sphere buffer
        size_t shaderRecordSize = Align(alignedGroupHandleSize + 8, alignedGroupHandleSize);

        CHECK_CALL(CreateBuffer(
            pRenderer,                // pRenderer
            shaderRecordSize,         // srcSize
            nullptr,                  // pSrcData
            usageFlags,               // usageFlags
            shaderGroupBaseAlignment, // minAlignment
            pHitGroupSBT));           // pBuffer

        // Copy shader handles
        {
            char* pData = nullptr;
            CHECK_CALL(vmaMapMemory(pRenderer->Allocator, pHitGroupSBT->Allocation, reinterpret_cast<void**>(&pData)));

            // Shader group handle
            memcpy(pData, pShaderGroupHandleHITG, groupHandleSize);
            pData += alignedGroupHandleSize;

            //
            // Device address for sphere buffer
            //
            // This isn't required to be done here. We can map and copy the
            // device address later if we want to.
            //
            VkDeviceAddress sphereBufferAddress = GetDeviceAddress(pRenderer, pSphereBuffer);
            memcpy(pData, &sphereBufferAddress, sizeof(sphereBufferAddress));

            vmaUnmapMemory(pRenderer->Allocator, pHitGroupSBT->Allocation);
        }
    }
}

void CreateBLAS(
    VulkanRenderer*     pRenderer,
    uint32_t            numSpheres,
    const VulkanBuffer* pSphereBuffer,
    VulkanAccelStruct*  pBLAS)
{
    // Get acceleration structure build size
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    {
        // Geometry
        VkAccelerationStructureGeometryKHR geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType                       = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs.sType               = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.data.deviceAddress  = GetDeviceAddress(pRenderer, pSphereBuffer);
        geometry.geometry.aabbs.stride              = sizeof(SphereFlake);

        // Build geometry info
        VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        //
        buildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildGeometryInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeometryInfo.geometryCount = 1;
        buildGeometryInfo.pGeometries   = &geometry;

        const uint32_t maxPrimitiveCount = numSpheres;
        fn_vkGetAccelerationStructureBuildSizesKHR(
            pRenderer->Device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeometryInfo,
            &maxPrimitiveCount,
            &buildSizesInfo);
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
        createInfo.deviceAddress                        = 0;

        CHECK_CALL(fn_vkCreateAccelerationStructureKHR(pRenderer->Device, &createInfo, nullptr, &pBLAS->AccelStruct));
    }

    // Create scratch buffer
    VulkanBuffer scratchBuffer = {};
    {
        // Get acceleration structure properties
        //
        // Obviously this can be cached if it's accessed frequently.
        //
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2                        properties            = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                                                         = &accelStructProperties;
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

    // Build acceleration structure
    //
    // You can use the geometry and build geometry info that was used to get
    // the build sizes. We don't do it to illustrate that they can also
    // be independent.
    //
    {
        // Geometry
        VkAccelerationStructureGeometryKHR geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType                       = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs.sType               = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.data.deviceAddress  = GetDeviceAddress(pRenderer, pSphereBuffer);
        geometry.geometry.aabbs.stride              = sizeof(SphereFlake);

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
        buildRangeInfo.primitiveCount                           = numSpheres;

        CommandObjects cmdBuf = {};
        CHECK_CALL(CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf));

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
        fn_vkCmdBuildAccelerationStructuresKHR(cmdBuf.CommandBuffer, 1, &buildGeometryInfo, &pBuildRangeInfo);

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        CHECK_CALL(ExecuteCommandBuffer(pRenderer, &cmdBuf));

        if (!WaitForGpu(pRenderer))
        {
            assert(false && "WaitForGpu failed");
        }
    }

    DestroyBuffer(pRenderer, &scratchBuffer);
}

void CreateTLAS(VulkanRenderer* pRenderer, const VulkanAccelStruct* pBLAS, VulkanAccelStruct* pTLAS)
{
    // clang-format off
	VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f 
    };
    // clang-format on

    VkAccelerationStructureInstanceKHR instance     = {};
    instance.transform                              = transformMatrix;
    instance.instanceCustomIndex                    = 0;
    instance.mask                                   = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference         = GetDeviceAddress(pRenderer, pBLAS->AccelStruct);

    // Instance buffer
    //
    // NOTE: Vulkan requires this buffer to be 16 bytes aligned
    //
    VulkanBuffer instanceBuffer;
    {
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

        CHECK_CALL(CreateBuffer(
            pRenderer,         // pRenderer
            sizeof(instance),  // srcSize
            &instance,         // pSrcData
            usageFlags,        // usageFlags
            16,                // minAlignment
            &instanceBuffer)); // pBuffer
    }

    // Get acceleration structure build size
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    {
        // Geometry
        VkAccelerationStructureGeometryKHR geometry    = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.triangles.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = GetDeviceAddress(pRenderer, &instanceBuffer);

        // Build geometry info
        VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        //
        buildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildGeometryInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeometryInfo.geometryCount = 1;
        buildGeometryInfo.pGeometries   = &geometry;

        const uint32_t maxPrimitiveCount = 1;
        fn_vkGetAccelerationStructureBuildSizesKHR(
            pRenderer->Device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeometryInfo,
            &maxPrimitiveCount,
            &buildSizesInfo);
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
        createInfo.deviceAddress                        = 0;

        CHECK_CALL(fn_vkCreateAccelerationStructureKHR(pRenderer->Device, &createInfo, nullptr, &pTLAS->AccelStruct));
    }

    // Create scratch buffer
    VulkanBuffer scratchBuffer = {};
    {
        // Get acceleration structure properties
        //
        // Obviously this can be cached if it's accessed frequently.
        //
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2                        properties            = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                                                         = &accelStructProperties;
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

    // Build acceleration structure
    {
        // Geometry
        VkAccelerationStructureGeometryKHR geometry    = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.triangles.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = GetDeviceAddress(pRenderer, &instanceBuffer);

        // Build geometry info
        VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        //
        buildGeometryInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildGeometryInfo.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildGeometryInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeometryInfo.dstAccelerationStructure  = pTLAS->AccelStruct;
        buildGeometryInfo.geometryCount             = 1;
        buildGeometryInfo.pGeometries               = &geometry;
        buildGeometryInfo.scratchData.deviceAddress = GetDeviceAddress(pRenderer, &scratchBuffer);

        // Build range info
        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {};
        buildRangeInfo.primitiveCount                           = 1;

        CommandObjects cmdBuf = {};
        CHECK_CALL(CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf));

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
        fn_vkCmdBuildAccelerationStructuresKHR(cmdBuf.CommandBuffer, 1, &buildGeometryInfo, &pBuildRangeInfo);

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        CHECK_CALL(ExecuteCommandBuffer(pRenderer, &cmdBuf));

        if (!WaitForGpu(pRenderer))
        {
            assert(false && "WaitForGpu failed");
        }
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
    auto mat           = glm::lookAt(vec3(0, 4, 3), vec3(0, 1, 0), vec3(0, 1, 0));
    camera.viewInverse = glm::inverse(mat);

    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

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
    VulkanAccelStruct*   pTLAS,
    VkImageView          pBackBuffer,
    VulkanBuffer*        pCameraBuffer)
{
    // layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
    VulkanAccelerationDescriptor topLevelASDescriptor;
    CreateDescriptor(
        pRenderer,
        &topLevelASDescriptor,
        0, // binding
        0, // arrayElement
        pTLAS);

    // layout(binding = 1, set = 0, rgba8) uniform image2D image;
    VulkanImageDescriptor backbufferDescriptor;
    CreateDescriptor(
        pRenderer,
        &backbufferDescriptor,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        1, // binding
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
            topLevelASDescriptor.layoutBinding,
            backbufferDescriptor.layoutBinding,
            cameraPropertiesDescriptor.layoutBinding,
        };

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
        {
            topLevelASDescriptor.writeDescriptorSet,
            backbufferDescriptor.writeDescriptorSet,
            cameraPropertiesDescriptor.writeDescriptorSet,
        };

    DestroyDescriptorSet(pRenderer, pDescriptors);
    CreateAndUpdateDescriptorSet(pRenderer, layoutBindings, writeDescriptorSets, pDescriptors);
}
