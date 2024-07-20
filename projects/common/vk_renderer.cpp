#include "vk_renderer.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "glslang/Include/glslang_c_interface.h"
#include "glslang/Public/resource_limits_c.h"

#if defined(GREX_ENABLE_SLANG)
#include "slang.h"
#include "slang-com-ptr.h"
#endif

#define VK_KHR_VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"

#define VK_QUEUE_MASK_ALL_TYPES (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
#define VK_QUEUE_MASK_GRAPHICS  (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
#define VK_QUEUE_MASK_COMPUTE   (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
#define VK_QUEUE_MASK_TRANSFER  (VK_QUEUE_TRANSFER_BIT)

#define DEFAULT_MIN_ALIGNMENT_SIZE 256

#if !defined(_WIN32)
template <typename T>
using ComPtr = CComPtr<T>;
#endif

PFN_vkCreateRayTracingPipelinesKHR             fn_vkCreateRayTracingPipelinesKHR             = nullptr;
PFN_vkGetRayTracingShaderGroupHandlesKHR       fn_vkGetRayTracingShaderGroupHandlesKHR       = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR    fn_vkGetAccelerationStructureBuildSizesKHR    = nullptr;
PFN_vkCreateAccelerationStructureKHR           fn_vkCreateAccelerationStructureKHR           = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR        fn_vkCmdBuildAccelerationStructuresKHR        = nullptr;
PFN_vkCmdTraceRaysKHR                          fn_vkCmdTraceRaysKHR                          = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR fn_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
PFN_vkGetDescriptorSetLayoutSizeEXT            fn_vkGetDescriptorSetLayoutSizeEXT            = nullptr;
PFN_vkGetDescriptorSetLayoutBindingOffsetEXT   fn_vkGetDescriptorSetLayoutBindingOffsetEXT   = nullptr;
PFN_vkGetDescriptorEXT                         fn_vkGetDescriptorEXT                         = nullptr;
PFN_vkCmdBindDescriptorBuffersEXT              fn_vkCmdBindDescriptorBuffersEXT              = nullptr;
PFN_vkCmdSetDescriptorBufferOffsetsEXT         fn_vkCmdSetDescriptorBufferOffsetsEXT         = nullptr;
PFN_vkCmdDrawMeshTasksEXT                      fn_vkCmdDrawMeshTasksEXT                      = nullptr;
PFN_vkCmdPushDescriptorSetKHR                  fn_vkCmdPushDescriptorSetKHR                  = nullptr;
PFN_vkCmdDrawMeshTasksNV                       fn_vkCmdDrawMeshTasksNV                       = nullptr;

bool     IsCompressed(VkFormat fmt);
uint32_t BitsPerPixel(VkFormat fmt);

std::vector<std::string> EnumeratePhysicalDeviceExtensionNames(VkPhysicalDevice physicalDevice)
{
    uint32_t count = 0;
    VkResult vkres = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    if (vkres != VK_SUCCESS)
    {
        return {};
    }
    std::vector<VkExtensionProperties> propertiesList(count);
    vkres = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, DataPtr(propertiesList));
    if (vkres != VK_SUCCESS)
    {
        return {};
    }
    std::vector<std::string> names;
    for (auto& properties : propertiesList)
    {
        names.push_back(properties.extensionName);
    }
    return names;
}

// =================================================================================================
// VkRenderer
// =================================================================================================
VulkanRenderer::VulkanRenderer()
{
}

VulkanRenderer::~VulkanRenderer()
{
}

CommandObjects::~CommandObjects()
{
    if (CommandBuffer != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(this->pRenderer->Device, this->CommandPool, 1, &CommandBuffer);
    }

    if (CommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(this->pRenderer->Device, this->CommandPool, nullptr);
    }
}

bool InitVulkan(VulkanRenderer* pRenderer, bool enableDebug, const VulkanFeatures& features, uint32_t apiVersion)
{
    if (IsNull(pRenderer))
    {
        return false;
    }

    pRenderer->DebugEnabled = enableDebug;
    pRenderer->Features     = features;

    // Instance
    {
        VkApplicationInfo appInfo  = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pNext              = nullptr;
        appInfo.pApplicationName   = "GREX App";
        appInfo.applicationVersion = 0;
        appInfo.pEngineName        = "GREX Engine";
        appInfo.engineVersion      = 0;
        appInfo.apiVersion         = apiVersion;

        std::vector<const char*> enabledLayers = {};
        if (pRenderer->DebugEnabled)
        {
            enabledLayers.push_back(VK_KHR_VALIDATION_LAYER_NAME);
        }

        std::vector<const char*> enabledExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
            VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
            VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
        };

        VkInstanceCreateInfo vkci    = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        vkci.pNext                   = nullptr;
        vkci.flags                   = 0;
        vkci.pApplicationInfo        = &appInfo;
        vkci.enabledLayerCount       = CountU32(enabledLayers);
        vkci.ppEnabledLayerNames     = DataPtr(enabledLayers);
        vkci.enabledExtensionCount   = CountU32(enabledExtensions);
        vkci.ppEnabledExtensionNames = DataPtr(enabledExtensions);

        VkResult vkres = vkCreateInstance(&vkci, nullptr, &pRenderer->Instance);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkCreateInstance failed");
            return false;
        }
    }

    // Load function
    {
        fn_vkCreateRayTracingPipelinesKHR             = (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkCreateRayTracingPipelinesKHR");
        fn_vkGetRayTracingShaderGroupHandlesKHR       = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkGetRayTracingShaderGroupHandlesKHR");
        fn_vkGetAccelerationStructureBuildSizesKHR    = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkGetAccelerationStructureBuildSizesKHR");
        fn_vkCreateAccelerationStructureKHR           = (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkCreateAccelerationStructureKHR");
        fn_vkCmdBuildAccelerationStructuresKHR        = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdBuildAccelerationStructuresKHR");
        fn_vkCmdTraceRaysKHR                          = (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdTraceRaysKHR");
        fn_vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkGetAccelerationStructureDeviceAddressKHR");
        fn_vkGetDescriptorSetLayoutSizeEXT            = (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetInstanceProcAddr(pRenderer->Instance, "vkGetDescriptorSetLayoutSizeEXT");
        fn_vkGetDescriptorSetLayoutBindingOffsetEXT   = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetInstanceProcAddr(pRenderer->Instance, "vkGetDescriptorSetLayoutBindingOffsetEXT");
        fn_vkGetDescriptorEXT                         = (PFN_vkGetDescriptorEXT)vkGetInstanceProcAddr(pRenderer->Instance, "vkGetDescriptorEXT");
        fn_vkCmdBindDescriptorBuffersEXT              = (PFN_vkCmdBindDescriptorBuffersEXT)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdBindDescriptorBuffersEXT");
        fn_vkCmdSetDescriptorBufferOffsetsEXT         = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdSetDescriptorBufferOffsetsEXT");
        fn_vkCmdDrawMeshTasksEXT                      = (PFN_vkCmdDrawMeshTasksEXT)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdDrawMeshTasksEXT");
        fn_vkCmdPushDescriptorSetKHR                  = (PFN_vkCmdPushDescriptorSetKHR)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdPushDescriptorSetKHR");
        fn_vkCmdDrawMeshTasksNV                       = (PFN_vkCmdDrawMeshTasksNV)vkGetInstanceProcAddr(pRenderer->Instance, "vkCmdDrawMeshTasksNV");
    }

    // Physical device
    {
        uint32_t count = 0;
        VkResult vkres = vkEnumeratePhysicalDevices(pRenderer->Instance, &count, nullptr);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkEnumeratePhysicalDevices failed");
            return false;
        }

        std::vector<VkPhysicalDevice> enumeratedPhysicalDevices(count);
        vkres = vkEnumeratePhysicalDevices(pRenderer->Instance, &count, DataPtr(enumeratedPhysicalDevices));
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkEnumeratePhysicalDevices failed");
            return false;
        }

        std::vector<VkPhysicalDevice> physicalDevices;
        for (auto& physicalDevice : enumeratedPhysicalDevices)
        {
            VkPhysicalDeviceProperties properties = {};
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);
            if ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
                (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU))
            {
                physicalDevices.push_back(physicalDevice);
            }
        }

        if (physicalDevices.empty())
        {
            assert(false && "No adapters found");
            return false;
        }

        pRenderer->PhysicalDevice = physicalDevices[0];
    }

    // Graphics queue family index
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pRenderer->PhysicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> propertiesList(count);
        vkGetPhysicalDeviceQueueFamilyProperties(pRenderer->PhysicalDevice, &count, DataPtr(propertiesList));

        for (uint32_t i = 0; i < count; ++i)
        {
            auto& properties = propertiesList[i];
            if ((properties.queueFlags & VK_QUEUE_MASK_ALL_TYPES) == VK_QUEUE_MASK_GRAPHICS)
            {
                pRenderer->GraphicsQueueFamilyIndex = i;
                break;
            }
        }

        if (pRenderer->GraphicsQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED)
        {
            assert(false && "Graphic queue family index not found");
            return false;
        }
    }

    // Device
    {
        const float kQueuePriority = 1.0f;

        VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.pNext                   = nullptr;
        queueCreateInfo.flags                   = 0;
        queueCreateInfo.queueFamilyIndex        = pRenderer->GraphicsQueueFamilyIndex;
        queueCreateInfo.queueCount              = 1;
        queueCreateInfo.pQueuePriorities        = &kQueuePriority;

        std::vector<const char*> enabledExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME};

        if (pRenderer->Features.EnableDescriptorBuffer)
        {
            enabledExtensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        }

        if (pRenderer->Features.EnableRayTracing)
        {
            enabledExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
        }

        if (pRenderer->Features.EnableMeshShader)
        {
            enabledExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
        }

        if (pRenderer->Features.EnablePushDescriptor)
        {
            enabledExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        }

        // Make sure all the extenions are present
        auto enumeratedExtensions = EnumeratePhysicalDeviceExtensionNames(pRenderer->PhysicalDevice);
        for (auto& elem : enabledExtensions)
        {
            if (!Contains(std::string(elem), enumeratedExtensions))
            {
                GREX_LOG_ERROR("extension not found: " << elem);
                assert(false);
                return false;
            }
        }

        // Check for mesh shader queries because some GPUs don't support it
        {
            VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

            VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext                     = &meshShaderFeatures;

            vkGetPhysicalDeviceFeatures2(pRenderer->PhysicalDevice, &features);
            pRenderer->HasMeshShaderQueries = meshShaderFeatures.meshShaderQueries;
        }

        // ---------------------------------------------------------------------

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures      = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        accelerationStructureFeatures.accelerationStructure                                 = VK_TRUE;
        accelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;

        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &accelerationStructureFeatures};
        rayQueryFeatures.rayQuery                            = VK_TRUE;

        VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rayTracingMaintenance1eFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, &rayQueryFeatures};
        rayTracingMaintenance1eFeatures.rayTracingMaintenance1                            = VK_TRUE;

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &rayTracingMaintenance1eFeatures};
        rayTracingPipelineFeatures.rayTracingPipeline                            = VK_TRUE;

        // ---------------------------------------------------------------------
        VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR shaderBarycentricFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR};
        shaderBarycentricFeatures.fragmentShaderBarycentric                            = VK_TRUE;

        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
        meshShaderFeatures.pNext                                 = &shaderBarycentricFeatures;
        meshShaderFeatures.taskShader                            = VK_TRUE;
        meshShaderFeatures.meshShader                            = VK_TRUE;
        meshShaderFeatures.meshShaderQueries                     = pRenderer->HasMeshShaderQueries ? VK_TRUE : VK_FALSE;

        // ---------------------------------------------------------------------

        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
        bufferDeviceAddressFeatures.pNext                                       = nullptr;
        bufferDeviceAddressFeatures.bufferDeviceAddress                         = VK_TRUE;

        //
        // Optional features
        //
        VkBaseInStructure* pStruct = reinterpret_cast<VkBaseInStructure*>(&bufferDeviceAddressFeatures);
        if (pRenderer->Features.EnableRayTracing)
        {
            pStruct->pNext = reinterpret_cast<VkBaseInStructure*>(&rayTracingPipelineFeatures);
            // accelerationStructure is the end of the ray tracing features chain
            pStruct = reinterpret_cast<VkBaseInStructure*>(&accelerationStructureFeatures);
        }
        if (pRenderer->Features.EnableMeshShader)
        {
            pStruct->pNext = reinterpret_cast<VkBaseInStructure*>(&meshShaderFeatures);
            pStruct        = reinterpret_cast<VkBaseInStructure*>(&meshShaderFeatures);
        }

        VkPhysicalDeviceImagelessFramebufferFeatures imagelessFramebufferFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES, &bufferDeviceAddressFeatures};
        imagelessFramebufferFeatures.imagelessFramebuffer                         = VK_TRUE;

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures         = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &imagelessFramebufferFeatures};
        descriptorIndexingFeatures.shaderInputAttachmentArrayDynamicIndexing          = VK_TRUE;
        descriptorIndexingFeatures.shaderUniformTexelBufferArrayDynamicIndexing       = VK_TRUE;
        descriptorIndexingFeatures.shaderStorageTexelBufferArrayDynamicIndexing       = VK_TRUE;
        descriptorIndexingFeatures.shaderUniformBufferArrayNonUniformIndexing         = VK_TRUE;
        descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing          = VK_TRUE;
        descriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing         = VK_TRUE;
        descriptorIndexingFeatures.shaderStorageImageArrayNonUniformIndexing          = VK_TRUE;
        descriptorIndexingFeatures.shaderInputAttachmentArrayNonUniformIndexing       = VK_TRUE;
        descriptorIndexingFeatures.shaderUniformTexelBufferArrayNonUniformIndexing    = VK_TRUE;
        descriptorIndexingFeatures.shaderStorageTexelBufferArrayNonUniformIndexing    = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind      = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind       = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind       = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind      = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending          = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingPartiallyBound                    = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount           = VK_TRUE;
        descriptorIndexingFeatures.runtimeDescriptorArray                             = VK_TRUE;

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, &descriptorIndexingFeatures};
        dynamicRenderingFeatures.dynamicRendering                         = VK_TRUE;

        VkPhysicalDeviceSynchronization2Features synchronization2Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, &dynamicRenderingFeatures};
        synchronization2Features.synchronization2                         = VK_TRUE;

        VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, &synchronization2Features};
        timelineSemaphoreFeatures.timelineSemaphore                         = VK_TRUE;

        VkPhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayoutFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES, &timelineSemaphoreFeatures};
        scalarBlockLayoutFeatures.scalarBlockLayout                         = VK_TRUE;

        VkPhysicalDeviceRobustness2FeaturesEXT robustness2Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, &scalarBlockLayoutFeatures};
        robustness2Features.nullDescriptor                         = VK_TRUE;

        VkPhysicalDeviceFeatures enabledFeatures = {};
        enabledFeatures.pipelineStatisticsQuery  = VK_TRUE;

        VkDeviceCreateInfo vkci      = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        vkci.pNext                   = &robustness2Features;
        vkci.flags                   = 0;
        vkci.queueCreateInfoCount    = 1;
        vkci.pQueueCreateInfos       = &queueCreateInfo;
        vkci.enabledLayerCount       = 0;
        vkci.ppEnabledLayerNames     = nullptr;
        vkci.enabledExtensionCount   = CountU32(enabledExtensions);
        vkci.ppEnabledExtensionNames = DataPtr(enabledExtensions);
        vkci.pEnabledFeatures        = &enabledFeatures;

        VkResult vkres = vkCreateDevice(pRenderer->PhysicalDevice, &vkci, nullptr, &pRenderer->Device);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkCreateDevice failed");
            return false;
        }

        VkPhysicalDeviceProperties deviceProperties = {};
        vkGetPhysicalDeviceProperties(pRenderer->PhysicalDevice, &deviceProperties);
        GREX_LOG_INFO("Created device using " << deviceProperties.deviceName);
    }

    // Queue
    {
        vkGetDeviceQueue(pRenderer->Device, pRenderer->GraphicsQueueFamilyIndex, 0, &pRenderer->Queue);
    }

    // VMA
    {
        VmaAllocatorCreateInfo vmaci = {};
        vmaci.flags                  = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vmaci.physicalDevice         = pRenderer->PhysicalDevice;
        vmaci.device                 = pRenderer->Device;
        vmaci.instance               = pRenderer->Instance;

        VkResult vkres = vmaCreateAllocator(&vmaci, &pRenderer->Allocator);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vmaCreateAllocator failed");
            return false;
        }
    }

    return true;
}

bool InitSwapchain(VulkanRenderer* pRenderer, VkSurfaceKHR surface, uint32_t width, uint32_t height, uint32_t imageCount)
{
    assert(surface != VK_NULL_HANDLE);

    pRenderer->Surface = surface;

    // Surface caps
    VkSurfaceCapabilitiesKHR surfaceCaps = {};
    {
        VkResult vkres = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pRenderer->PhysicalDevice, pRenderer->Surface, &surfaceCaps);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
            return false;
        }
    }

    // Swapchain
    {
        imageCount = std::max<uint32_t>(imageCount, surfaceCaps.minImageCount);
        if (surfaceCaps.maxImageCount > 0)
        {
            imageCount = std::min<uint32_t>(imageCount, surfaceCaps.maxImageCount);
        }

        VkSwapchainCreateInfoKHR vkci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        //
        vkci.pNext                 = nullptr;
        vkci.flags                 = 0;
        vkci.surface               = pRenderer->Surface;
        vkci.minImageCount         = imageCount;
        vkci.imageFormat           = GREX_DEFAULT_RTV_FORMAT;
        vkci.imageColorSpace       = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        vkci.imageExtent           = {width, height};
        vkci.imageArrayLayers      = 1;
        vkci.imageUsage            = surfaceCaps.supportedUsageFlags;
        vkci.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        vkci.queueFamilyIndexCount = 0;
        vkci.pQueueFamilyIndices   = nullptr;
        vkci.preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        vkci.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        vkci.presentMode           = VK_PRESENT_MODE_FIFO_KHR;
        vkci.clipped               = VK_FALSE;
        vkci.oldSwapchain          = VK_NULL_HANDLE;

        VkResult vkres = vkCreateSwapchainKHR(pRenderer->Device, &vkci, nullptr, &pRenderer->Swapchain);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkCreateSwapchainKHR failed");
            return false;
        }

        pRenderer->SwapchainImageCount = imageCount;
        pRenderer->SwapchainImageUsage = vkci.imageUsage;
    }

    // Transition image layouts to present
    {
        std::vector<VkImage> images;
        VkResult             vkres = GetSwapchainImages(pRenderer, images);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "GetSwapchainImages failed");
            return false;
        }

        for (auto& image : images)
        {
            vkres = TransitionImageLayout(pRenderer, image, GREX_ALL_SUBRESOURCES, VK_IMAGE_ASPECT_COLOR_BIT, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_PRESENT);
            if (vkres != VK_SUCCESS)
            {
                assert(false && "TransitionImageLayout failed");
                return false;
            }
        }
    }

    // Semaphores
    {
        VkSemaphoreCreateInfo vkci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkci.pNext                 = nullptr;
        vkci.flags                 = 0;

        VkResult vkres = vkCreateSemaphore(pRenderer->Device, &vkci, nullptr, &pRenderer->ImageReadySemaphore);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkCreateSemaphore failed");
            return false;
        }

        vkres = vkCreateSemaphore(pRenderer->Device, &vkci, nullptr, &pRenderer->PresentReadySemaphore);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkCreateSemaphore failed");
            return false;
        }
    }

    // Fence
    {
        VkFenceCreateInfo vkci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkci.pNext             = nullptr;
        vkci.flags             = 0;

        VkResult vkres = vkCreateFence(pRenderer->Device, &vkci, nullptr, &pRenderer->ImageReadyFence);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkCreateFence failed");
            return false;
        }
    }

    return true;
}

bool WaitForGpu(VulkanRenderer* pRenderer)
{
    VkResult vkres = vkQueueWaitIdle(pRenderer->Queue);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkQueueWaitIdle failed");
        return false;
    }

    return true;
}

bool WaitForFence(VulkanRenderer* pRenderer, VkFence fence)
{
    VkResult vkres = vkWaitForFences(pRenderer->Device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkWaitForFences failed");
        return false;
    }

    vkres = vkResetFences(pRenderer->Device, 1, &fence);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkWaitForFences failed");
        return false;
    }

    return true;
}

VkResult GetSwapchainImages(VulkanRenderer* pRenderer, std::vector<VkImage>& images)
{
    uint32_t count = 0;
    VkResult vkres = vkGetSwapchainImagesKHR(pRenderer->Device, pRenderer->Swapchain, &count, nullptr);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkGetSwapchainImagesKHR failed");
        return vkres;
    }
    images.resize(count);
    vkres = vkGetSwapchainImagesKHR(pRenderer->Device, pRenderer->Swapchain, &count, DataPtr(images));
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkGetSwapchainImagesKHR failed");
        return vkres;
    }
    return VK_SUCCESS;
}

VkResult AcquireNextImage(VulkanRenderer* pRenderer, uint32_t* pImageIndex)
{
    VkResult vkres = vkAcquireNextImageKHR(pRenderer->Device, pRenderer->Swapchain, UINT64_MAX, VK_NULL_HANDLE, pRenderer->ImageReadyFence, pImageIndex);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkAcquireNextImageKHR failed");
        return vkres;
    }

    vkres = vkWaitForFences(pRenderer->Device, 1, &pRenderer->ImageReadyFence, VK_TRUE, UINT64_MAX);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkWaitForFences failed");
        return vkres;
    }

    vkres = vkResetFences(pRenderer->Device, 1, &pRenderer->ImageReadyFence);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkResetFences failed");
        return vkres;
    }

    return VK_SUCCESS;
}

bool SwapchainPresent(VulkanRenderer* pRenderer, uint32_t imageIndex)
{
    VkPresentInfoKHR presentInfo   = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pNext              = nullptr;
    presentInfo.waitSemaphoreCount = 0;
    presentInfo.pWaitSemaphores    = nullptr;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &pRenderer->Swapchain;
    presentInfo.pImageIndices      = &imageIndex;
    presentInfo.pResults           = nullptr;

    VkResult vkres = vkQueuePresentKHR(pRenderer->Queue, &presentInfo);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkQueuePresentKHR failed");
        return false;
    }

    return true;
}

VkFormat ToVkFormat(GREXFormat format)
{
    // clang-format off
    switch (format) {
        default: break;
        case GREX_FORMAT_R8_UNORM           : return VK_FORMAT_R8_UNORM;
        case GREX_FORMAT_R8G8_UNORM         : return VK_FORMAT_R8G8_UNORM;
        case GREX_FORMAT_R8G8B8A8_UNORM     : return VK_FORMAT_R8G8B8A8_UNORM;
        case GREX_FORMAT_R8_UINT            : return VK_FORMAT_R8_UINT;
        case GREX_FORMAT_R16_UINT           : return VK_FORMAT_R16_UINT;
        case GREX_FORMAT_R32_UINT           : return VK_FORMAT_R32_UINT;
        case GREX_FORMAT_R32_FLOAT          : return VK_FORMAT_R32_SFLOAT;
        case GREX_FORMAT_R32G32_FLOAT       : return VK_FORMAT_R32G32_SFLOAT;
        case GREX_FORMAT_R32G32B32A32_FLOAT : return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GREX_FORMAT_BC1_RGB            : return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case GREX_FORMAT_BC3_RGBA           : return VK_FORMAT_BC3_UNORM_BLOCK;
        case GREX_FORMAT_BC4_R              : return VK_FORMAT_BC4_UNORM_BLOCK;
        case GREX_FORMAT_BC5_RG             : return VK_FORMAT_BC5_UNORM_BLOCK;
        case GREX_FORMAT_BC6H_SFLOAT        : return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case GREX_FORMAT_BC6H_UFLOAT        : return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case GREX_FORMAT_BC7_RGBA           : return VK_FORMAT_BC7_UNORM_BLOCK;
    }
    // clang-format on
    return VK_FORMAT_UNDEFINED;
}

VkIndexType ToVkIndexType(GREXFormat format)
{
    // clang-format off
    switch (format) {
        default: break;
        case GREX_FORMAT_R8_UINT            : return VK_INDEX_TYPE_UINT8_EXT;
        case GREX_FORMAT_R16_UINT           : return VK_INDEX_TYPE_UINT16;
        case GREX_FORMAT_R32_UINT           : return VK_INDEX_TYPE_UINT32;
    }
    // clang-format on
    return VK_INDEX_TYPE_NONE_KHR;
}

VkResult CreateCommandBuffer(VulkanRenderer* pRenderer, VkCommandPoolCreateFlags poolCreateFlags, CommandObjects* pCmdBuf)
{
    if (IsNull(pCmdBuf))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *pCmdBuf = {pRenderer};

    VkCommandPoolCreateInfo vkci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    vkci.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | poolCreateFlags;
    vkci.queueFamilyIndex        = pRenderer->GraphicsQueueFamilyIndex;

    VkResult vkres = vkCreateCommandPool(pRenderer->Device, &vkci, nullptr, &pCmdBuf->CommandPool);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkCreateCommandPool failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBufferAllocateInfo vkai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    vkai.commandPool                 = pCmdBuf->CommandPool;
    vkai.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkai.commandBufferCount          = 1;

    vkres = vkAllocateCommandBuffers(pRenderer->Device, &vkai, &pCmdBuf->CommandBuffer);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkAllocateCommandBuffers failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return VK_SUCCESS;
}

void DestroyCommandBuffer(VulkanRenderer* pRenderer, CommandObjects* pCmdBuf)
{
    vkFreeCommandBuffers(
        pRenderer->Device,
        pCmdBuf->CommandPool,
        1,
        &pCmdBuf->CommandBuffer);
    pCmdBuf->CommandBuffer = VK_NULL_HANDLE;

    vkDestroyCommandPool(
        pRenderer->Device,
        pCmdBuf->CommandPool,
        nullptr);
    pCmdBuf->CommandPool = VK_NULL_HANDLE;
}

VkResult ExecuteCommandBuffer(VulkanRenderer* pRenderer, const CommandObjects* pCmdBuf, VkFence fence)
{
    if (IsNull(pCmdBuf))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBufferSubmitInfo cmdSubmitinfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdSubmitinfo.pNext                     = nullptr;
    cmdSubmitinfo.commandBuffer             = pCmdBuf->CommandBuffer;
    cmdSubmitinfo.deviceMask                = 0;

    VkSubmitInfo2 submitInfo            = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.pNext                    = nullptr;
    submitInfo.flags                    = 0;
    submitInfo.waitSemaphoreInfoCount   = 0;
    submitInfo.pWaitSemaphoreInfos      = nullptr;
    submitInfo.commandBufferInfoCount   = 1;
    submitInfo.pCommandBufferInfos      = &cmdSubmitinfo;
    submitInfo.signalSemaphoreInfoCount = 0;
    submitInfo.pSignalSemaphoreInfos    = nullptr;

    VkResult vkres = vkQueueSubmit2(
        pRenderer->Queue,
        1,
        &submitInfo,
        fence);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkEndCommandBuffer failed");
        return vkres;
    }

    return VK_SUCCESS;
}

bool ResourceStateToBarrierInfo(
    ResourceState          state,
    bool                   isDst,
    VkPipelineStageFlags2* pStageMask,
    VkAccessFlags2*        pAccessMask,
    VkImageLayout*         pLayout)
{
    VkPipelineStageFlags2 stage_mask  = static_cast<VkPipelineStageFlags2>(~0);
    VkAccessFlags2        access_mask = static_cast<VkAccessFlags2>(~0);
    VkImageLayout         layout      = static_cast<VkImageLayout>(~0);

    switch (state)
    {
        default:
        {
            return false;
        }
        break;

        case RESOURCE_STATE_UNKNOWN:
        {
            stage_mask  = 0;
            access_mask = 0;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        break;

        case RESOURCE_STATE_COMMON:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            access_mask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_VERTEX_AND_UNIFORM_BUFFER:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            access_mask = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        break;

        case RESOURCE_STATE_INDEX_BUFFER:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            access_mask = VK_ACCESS_2_INDEX_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        break;

        case RESOURCE_STATE_RENDER_TARGET:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_DEPTH_STENCIL:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_DEPTH_READ:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_STENCIL_READ:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_DEPTH_AND_STENCIL_READ:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_VERTEX_SHADER_RESOURCE:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_HULL_SHADER_RESOURCE:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_DOMAIN_SHADER_RESOURCE:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_GEOMETRY_SHADER_RESOURCE:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_COMPUTE_SHADER_RESOURCE:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_VERTEX_UNORDERED_ACCESS:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_HULL_UNORDERED_ACCESS:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_DOMAIN_UNORDERED_ACCESS:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_GEOMETRY_UNORDERED_ACCESS:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_PIXEL_UNORDERED_ACCESS:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_COMPUTE_UNORDERED_ACCESS:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        }
        break;

        case RESOURCE_STATE_TRANSFER_DST:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_COPY_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_TRANSFER_SRC:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_COPY_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_RESOLVE_DST:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_RESOLVE_SRC:
        {
            stage_mask  = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        break;

        case RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE:
        {
            stage_mask  = 0;
            access_mask = 0;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        break;

        case RESOURCE_STATE_PRESENT:
        {
            stage_mask  = 0;
            access_mask = 0;
            layout      = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        break;
    }

    if (!IsNull(pStageMask))
    {
        *pStageMask = stage_mask;
    }

    if (!IsNull(pAccessMask))
    {
        *pAccessMask = access_mask;
    }

    if (!IsNull(pLayout))
    {
        *pLayout = layout;
    }

    return true;
}

void CmdTransitionImageLayout(
    VkCommandBuffer    cmdBuf,
    VkImage            image,
    uint32_t           firstMip,
    uint32_t           mipCount,
    uint32_t           firstLayer,
    uint32_t           layerCount,
    VkImageAspectFlags aspectFlags,
    ResourceState      stateBefore,
    ResourceState      stateAfter)
{
    VkImageMemoryBarrier2 barrier           = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.pNext                           = nullptr;
    barrier.srcStageMask                    = 0;
    barrier.srcAccessMask                   = 0;
    barrier.dstStageMask                    = 0;
    barrier.dstAccessMask                   = 0;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = aspectFlags;
    barrier.subresourceRange.baseMipLevel   = firstMip;
    barrier.subresourceRange.levelCount     = mipCount;
    barrier.subresourceRange.baseArrayLayer = firstLayer;
    barrier.subresourceRange.layerCount     = layerCount;

    ResourceStateToBarrierInfo(stateBefore, false, &barrier.srcStageMask, &barrier.srcAccessMask, &barrier.oldLayout);
    ResourceStateToBarrierInfo(stateAfter, true, &barrier.dstStageMask, &barrier.dstAccessMask, &barrier.newLayout);

    VkDependencyInfo dependencyInfo         = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.pNext                    = nullptr;
    dependencyInfo.dependencyFlags          = 0;
    dependencyInfo.memoryBarrierCount       = 0;
    dependencyInfo.pMemoryBarriers          = nullptr;
    dependencyInfo.bufferMemoryBarrierCount = 0;
    dependencyInfo.pBufferMemoryBarriers    = nullptr;
    dependencyInfo.imageMemoryBarrierCount  = 1;
    dependencyInfo.pImageMemoryBarriers     = &barrier;

    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

VkResult TransitionImageLayout(
    VulkanRenderer*    pRenderer,
    VkImage            image,
    uint32_t           firstMip,
    uint32_t           mipCount,
    uint32_t           firstLayer,
    uint32_t           layerCount,
    VkImageAspectFlags aspectFlags,
    ResourceState      stateBefore,
    ResourceState      stateAfter)
{
    CommandObjects cmdBuf = {};
    VkResult       vkres  = CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "CreateCommandBuffer failed");
        return vkres;
    }

    VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkres = vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkBeginCommandBuffer failed");
        return vkres;
    }

    CmdTransitionImageLayout(
        cmdBuf.CommandBuffer,
        image,
        firstMip,
        mipCount,
        firstLayer,
        layerCount,
        aspectFlags,
        stateBefore,
        stateAfter);

    vkres = vkEndCommandBuffer(cmdBuf.CommandBuffer);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkEndCommandBuffer failed");
        return vkres;
    }

    vkres = ExecuteCommandBuffer(pRenderer, &cmdBuf);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "ExecuteCommandBuffer failed");
        return vkres;
    }

    vkres = vkQueueWaitIdle(pRenderer->Queue);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkQueueWaitIdle failed");
        return vkres;
    }

    DestroyCommandBuffer(pRenderer, &cmdBuf);

    return VK_SUCCESS;
}

VulkanImageDescriptor::VulkanImageDescriptor(size_t count)
{
    imageInfo.resize(count);
    for (auto& info : imageInfo)
    {
        info.imageView   = VK_NULL_HANDLE;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.sampler     = VK_NULL_HANDLE;
    }

    writeDescriptorSet.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.pNext            = nullptr;
    writeDescriptorSet.dstSet           = VK_NULL_HANDLE;
    writeDescriptorSet.dstBinding       = 0;
    writeDescriptorSet.dstArrayElement  = 0;
    writeDescriptorSet.descriptorCount  = 0;
    writeDescriptorSet.descriptorType   = static_cast<VkDescriptorType>(0);
    writeDescriptorSet.pImageInfo       = nullptr;
    writeDescriptorSet.pBufferInfo      = nullptr;
    writeDescriptorSet.pTexelBufferView = nullptr;
}

VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    VkBufferUsageFlags usageFlags,
    VulkanBuffer*      pSrcBuffer,
    VulkanBuffer*      pBuffer)
{
    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        usageFlags |= VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkResult vkres = CreateBuffer(
        pRenderer,
        pSrcBuffer->Size,
        usageFlags,
        VMA_MEMORY_USAGE_AUTO,
        0,
        pBuffer);

    if (vkres != VK_SUCCESS)
    {
        return vkres;
    }

    VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    CommandObjects cmdBuf = {};

    vkres = CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "CreateCommandBuffer failed");
        return vkres;
    }

    vkres = vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkBeginCommandBuffer failed");
        return vkres;
    }

    VkBufferCopy region = {};
    region.srcOffset    = 0;
    region.dstOffset    = 0;
    region.size         = pSrcBuffer->Size;

    vkCmdCopyBuffer(
        cmdBuf.CommandBuffer,
        pSrcBuffer->Buffer,
        pBuffer->Buffer,
        1,
        &region);

    vkres = vkEndCommandBuffer(cmdBuf.CommandBuffer);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkEndCommandBuffer failed");
        return vkres;
    }

    vkres = ExecuteCommandBuffer(pRenderer, &cmdBuf);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "ExecuteCommandBuffer failed");
        return vkres;
    }

    vkres = vkQueueWaitIdle(pRenderer->Queue);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "vkQueueWaitIdle failed");
        return vkres;
    }

    DestroyCommandBuffer(pRenderer, &cmdBuf);

    return vkres;
}

VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             size,
    VkBufferUsageFlags usageFlags,
    VmaMemoryUsage     memoryUsage,
    VkDeviceSize       minAlignment,
    VulkanBuffer*      pBuffer)
{
    assert((size > 0) && "Cannot create a buffer of size 0");

    if (IsNull(pBuffer))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferCreateInfo vkci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vkci.pNext              = nullptr;
    vkci.size               = size;
    vkci.usage              = usageFlags;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage                   = memoryUsage;

    if (minAlignment > 0)
    {
        VkResult vkres = vmaCreateBufferWithAlignment(
            pRenderer->Allocator,
            &vkci,
            &allocCreateInfo,
            minAlignment,
            &pBuffer->Buffer,
            &pBuffer->Allocation,
            &pBuffer->AllocationInfo);
        if (vkres != VK_SUCCESS)
        {
            return vkres;
        }
    }
    else
    {
        VkResult vkres = vmaCreateBuffer(
            pRenderer->Allocator,
            &vkci,
            &allocCreateInfo,
            &pBuffer->Buffer,
            &pBuffer->Allocation,
            &pBuffer->AllocationInfo);
        if (vkres != VK_SUCCESS)
        {
            return vkres;
        }
    }

    pBuffer->Allocator = pRenderer->Allocator;
    pBuffer->Size      = size;

    return VK_SUCCESS;
}

VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             srcSize,
    const void*        pSrcData,
    VkBufferUsageFlags usageFlags,
    VkDeviceSize       minAlignment,
    VulkanBuffer*      pBuffer)
{
    VkResult vkres = CreateBuffer(
        pRenderer,
        srcSize,
        usageFlags,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        minAlignment,
        pBuffer);
    if (vkres != VK_SUCCESS)
    {
        return vkres;
    }

    if (!IsNull(pSrcData))
    {
        char*    pData = nullptr;
        VkResult vkres = vmaMapMemory(pRenderer->Allocator, pBuffer->Allocation, reinterpret_cast<void**>(&pData));
        if (vkres != VK_SUCCESS)
        {
            return vkres;
        }

        memcpy(pData, pSrcData, srcSize);

        vmaUnmapMemory(pRenderer->Allocator, pBuffer->Allocation);
    }

    pBuffer->Allocator = pRenderer->Allocator;
    pBuffer->Size      = srcSize;

    return VK_SUCCESS;
}

VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             srcSize,
    const void*        pSrcData, // [OPTIONAL] NULL if no data
    VkBufferUsageFlags usageFlags,
    VmaMemoryUsage     memoryUsage,
    VkDeviceSize       minAlignment, // Use 0 for no alignment
    VulkanBuffer*      pBuffer)
{
    VkResult vkres = CreateBuffer(
        pRenderer,
        srcSize,
        usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        memoryUsage,
        minAlignment,
        pBuffer);
    if (vkres != VK_SUCCESS)
    {
        return vkres;
    }

    if ((srcSize > 0) && !IsNull(pSrcData))
    {
        VulkanBuffer stagingBuffer = {};
        //
        vkres = CreateBuffer(
            pRenderer,
            srcSize,
            pSrcData,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            0,
            &stagingBuffer);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "create staging buffer failed");
            return vkres;
        }

        CommandObjects cmdBuf = {};
        VkResult       vkres  = CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "CreateCommandBuffer failed");
            return vkres;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkres = vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkBeginCommandBuffer failed");
            return vkres;
        }

        VkBufferCopy region = {};
        region.srcOffset    = 0;
        region.dstOffset    = 0;
        region.size         = srcSize;

        vkCmdCopyBuffer(
            cmdBuf.CommandBuffer,
            stagingBuffer.Buffer,
            pBuffer->Buffer,
            1,
            &region);

        vkres = vkEndCommandBuffer(cmdBuf.CommandBuffer);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkEndCommandBuffer failed");
            return vkres;
        }

        vkres = ExecuteCommandBuffer(pRenderer, &cmdBuf);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "ExecuteCommandBuffer failed");
            return vkres;
        }

        vkres = vkQueueWaitIdle(pRenderer->Queue);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkQueueWaitIdle failed");
            return vkres;
        }

        DestroyCommandBuffer(pRenderer, &cmdBuf);
        DestroyBuffer(pRenderer, &stagingBuffer);
    }

    return VK_SUCCESS;
}

VkResult CreateImage(
    VulkanRenderer*   pRenderer,
    VkImageType       imageType,
    VkImageUsageFlags imageUsage,
    uint32_t          width,
    uint32_t          height,
    uint32_t          depth,
    VkFormat          format,
    uint32_t          numMipLevels,
    uint32_t          numArrayLayers,
    VkImageLayout     initialLayout,
    VmaMemoryUsage    memoryUsage,
    VulkanImage*      pImage)
{
    if (IsNull(pImage))
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageCreateInfo vkci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    vkci.imageType         = imageType;
    vkci.format            = format;
    vkci.extent.width      = width;
    vkci.extent.height     = height;
    vkci.extent.depth      = depth;
    vkci.mipLevels         = numMipLevels;
    vkci.arrayLayers       = numArrayLayers;
    vkci.usage             = imageUsage;
    vkci.initialLayout     = initialLayout;
    vkci.samples           = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage                   = memoryUsage;

    VkResult vkres = vmaCreateImage(
        pRenderer->Allocator,
        &vkci,
        &allocCreateInfo,
        &pImage->Image,
        &pImage->Allocation,
        &pImage->AllocationInfo);

    return vkres;
}

VkResult CreateTexture(
    VulkanRenderer*               pRenderer,
    uint32_t                      width,
    uint32_t                      height,
    VkFormat                      format,
    const std::vector<MipOffset>& mipOffsets,
    uint64_t                      srcSizeBytes,
    const void*                   pSrcData,
    VulkanImage*                  pImage)
{
    if (IsNull(pRenderer))
    {
        return VK_ERROR_UNKNOWN;
    }
    if (IsNull(pImage))
    {
        return VK_ERROR_UNKNOWN;
    }
    if (format == VK_FORMAT_UNDEFINED)
    {
        return VK_ERROR_UNKNOWN;
    }
    if (mipOffsets.empty())
    {
        return VK_ERROR_UNKNOWN;
    }

    uint32_t mipLevels = CountU32(mipOffsets);

    VkResult vkres = CreateImage(
        pRenderer,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        width,
        height,
        1,
        format,
        mipLevels,
        1,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VMA_MEMORY_USAGE_GPU_ONLY,
        pImage);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "create image failed");
        return vkres;
    }

    vkres = TransitionImageLayout(
        pRenderer,
        pImage->Image,
        GREX_ALL_SUBRESOURCES,
        VK_IMAGE_ASPECT_COLOR_BIT,
        RESOURCE_STATE_UNKNOWN,
        RESOURCE_STATE_TRANSFER_DST);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "transition image layout failed");
        return vkres;
    }

    if ((srcSizeBytes > 0) && !IsNull(pSrcData))
    {
        VulkanBuffer stagingBuffer = {};
        if (IsCompressed(format))
        {
            vkres = CreateBuffer(
                pRenderer,
                srcSizeBytes,
                pSrcData,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                DEFAULT_MIN_ALIGNMENT_SIZE,
                &stagingBuffer);
        }
        else
        {
            const uint32_t rowStride = width * BytesPerPixel(format);
            // Calculate the total number of rows for all mip maps
            uint32_t numRows = 0;
            {
                uint32_t mipHeight = height;
                for (UINT level = 0; level < mipLevels; ++level)
                {
                    numRows += mipHeight;
                    mipHeight >>= 1;
                }
            }

            //
            vkres = CreateBuffer(
                pRenderer,
                rowStride * numRows,
                pSrcData,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                DEFAULT_MIN_ALIGNMENT_SIZE,
                &stagingBuffer);
        }
        if (vkres != VK_SUCCESS)
        {
            assert(false && "create staging buffer failed");
            return vkres;
        }

        CommandObjects cmdBuf = {};
        VkResult       vkres  = CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "CreateCommandBuffer failed");
            return vkres;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkres = vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkBeginCommandBuffer failed");
            return vkres;
        }

        // Build command buffer
        {
            uint32_t levelWidth        = width;
            uint32_t levelHeight       = height;
            uint32_t formatSizeInBytes = BytesPerPixel(format);
            for (UINT level = 0; level < mipLevels; ++level)
            {
                const auto& mipOffset            = mipOffsets[level];
                uint32_t    mipRowStrideInPixels = mipOffset.RowStride / formatSizeInBytes;
                uint32_t    mipLevelHeight       = levelHeight;

                if (IsCompressed(format))
                {
                    //
                    // If it's compressed, just set the variables to zero and let the API figure it out based on the imageExtents
                    //
                    mipRowStrideInPixels = 0;
                    mipLevelHeight       = 0;
                }

                VkImageAspectFlagBits aspectFlags     = VK_IMAGE_ASPECT_COLOR_BIT;
                VkBufferImageCopy     srcRegion       = {};
                srcRegion.bufferOffset                = mipOffset.Offset;
                srcRegion.bufferRowLength             = mipRowStrideInPixels; // Row stride but in Pixels/texels
                srcRegion.bufferImageHeight           = mipLevelHeight;       // Pixels/texels
                srcRegion.imageSubresource.aspectMask = aspectFlags;
                srcRegion.imageSubresource.layerCount = 1;
                srcRegion.imageSubresource.mipLevel   = level;
                srcRegion.imageExtent.width           = levelWidth;
                srcRegion.imageExtent.height          = levelHeight;
                srcRegion.imageExtent.depth           = 1;

                vkCmdCopyBufferToImage(
                    cmdBuf.CommandBuffer,
                    stagingBuffer.Buffer,
                    pImage->Image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &srcRegion);

                levelWidth >>= 1;
                levelHeight >>= 1;
            }
        }

        vkres = vkEndCommandBuffer(cmdBuf.CommandBuffer);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkEndCommandBuffer failed");
            return vkres;
        }

        vkres = ExecuteCommandBuffer(pRenderer, &cmdBuf);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "ExecuteCommandBuffer failed");
            return vkres;
        }

        vkres = vkQueueWaitIdle(pRenderer->Queue);
        if (vkres != VK_SUCCESS)
        {
            assert(false && "vkQueueWaitIdle failed");
            return vkres;
        }

        DestroyCommandBuffer(pRenderer, &cmdBuf);
        DestroyBuffer(pRenderer, &stagingBuffer);
    }

    vkres = TransitionImageLayout(
        pRenderer,
        pImage->Image,
        GREX_ALL_SUBRESOURCES,
        VK_IMAGE_ASPECT_COLOR_BIT,
        RESOURCE_STATE_TRANSFER_DST,
        RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
    if (vkres != VK_SUCCESS)
    {
        assert(false && "transition image layout failed");
        return vkres;
    }

    return VK_SUCCESS;
}

VkResult CreateTexture(
    VulkanRenderer* pRenderer,
    uint32_t        width,
    uint32_t        height,
    VkFormat        format,
    uint64_t        srcSizeBytes,
    const void*     pSrcData,
    VulkanImage*    pImage)
{
    MipOffset mipOffset = {};
    mipOffset.Offset    = 0;
    mipOffset.RowStride = width * BytesPerPixel(format);

    return CreateTexture(
        pRenderer,
        width,
        height,
        format,
        {mipOffset},
        srcSizeBytes,
        pSrcData,
        pImage);
}

VkResult CreateImageView(
    VulkanRenderer*    pRenderer,
    const VulkanImage* pImage,
    VkImageViewType    viewType,
    VkFormat           format,
    uint32_t           firstMipLevel,
    uint32_t           numMipLevels,
    uint32_t           firstArrayLayer,
    uint32_t           numArrayLayers,
    VkImageView*       pImageView)
{
    VkImageViewCreateInfo createInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    createInfo.pNext                           = nullptr;
    createInfo.flags                           = 0;
    createInfo.image                           = pImage->Image;
    createInfo.viewType                        = viewType;
    createInfo.format                          = format;
    createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel   = firstMipLevel;
    createInfo.subresourceRange.levelCount     = numMipLevels;
    createInfo.subresourceRange.baseArrayLayer = firstArrayLayer;
    createInfo.subresourceRange.layerCount     = numArrayLayers;

    VkResult vkres = vkCreateImageView(
        pRenderer->Device,
        &createInfo,
        nullptr,
        pImageView);
    return vkres;
}

VkResult CreateDSV(
    VulkanRenderer* pRenderer,
    uint32_t        width,
    uint32_t        height,
    VulkanImage*    pImage)
{
    return CreateImage(
        pRenderer,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        width,
        height,
        1,
        GREX_DEFAULT_DSV_FORMAT,
        1,
        1,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VMA_MEMORY_USAGE_GPU_ONLY,
        pImage);
}

VkResult CreateRenderPass(
    VulkanRenderer*                          pRenderer,
    const std::vector<VulkanAttachmentInfo>& colorInfos,
    const VulkanAttachmentInfo&              depthStencilInfo,
    uint32_t                                 width,
    uint32_t                                 height,
    VulkanRenderPass*                        pRenderPass)
{
    bool hasDepthStencil = (depthStencilInfo.Format != VK_FORMAT_UNDEFINED);

    // Render pass
    {
        std::vector<VkAttachmentDescription> attachmentDescs;
        std::vector<VkAttachmentReference>   colorAttachmentRefs;
        for (uint32_t i = 0; i < CountU32(colorInfos); ++i)
        {
            VkAttachmentDescription desc = {};
            desc.flags                   = 0;
            desc.format                  = colorInfos[i].Format;
            desc.samples                 = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp                  = colorInfos[i].LoadOp;
            desc.storeOp                 = colorInfos[i].StoreOp;
            desc.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            desc.finalLayout             = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentDescs.push_back(desc);

            VkAttachmentReference attachmentRef = {};
            attachmentRef.attachment            = CountU32(attachmentDescs) - 1;
            attachmentRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachmentRefs.push_back(attachmentRef);
        }

        VkAttachmentReference depthStencilAttachmentRef = {};
        if (hasDepthStencil)
        {
            VkAttachmentDescription desc = {};
            desc.flags                   = 0;
            desc.format                  = depthStencilInfo.Format;
            desc.samples                 = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp                  = depthStencilInfo.LoadOp;
            desc.storeOp                 = depthStencilInfo.StoreOp;
            desc.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            desc.finalLayout             = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachmentDescs.push_back(desc);

            depthStencilAttachmentRef.attachment = CountU32(attachmentDescs) - 1;
            depthStencilAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpassDesc    = {};
        subpassDesc.flags                   = 0;
        subpassDesc.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount    = CountU32(colorAttachmentRefs);
        subpassDesc.pColorAttachments       = DataPtr(colorAttachmentRefs);
        subpassDesc.pDepthStencilAttachment = hasDepthStencil ? &depthStencilAttachmentRef : nullptr;

        VkSubpassDependency subpassDep = {};
        subpassDep.srcSubpass          = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass          = 0;
        subpassDep.srcStageMask        = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep.dstStageMask        = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask       = 0;
        subpassDep.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        subpassDep.dependencyFlags     = 0;

        VkRenderPassCreateInfo createInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        createInfo.flags                  = 0;
        createInfo.attachmentCount        = CountU32(attachmentDescs);
        createInfo.pAttachments           = DataPtr(attachmentDescs);
        createInfo.subpassCount           = 1;
        createInfo.pSubpasses             = &subpassDesc;
        createInfo.dependencyCount        = 1;
        createInfo.pDependencies          = &subpassDep;

        VkResult vkres = vkCreateRenderPass(
            pRenderer->Device,
            &createInfo,
            nullptr,
            &pRenderPass->RenderPass);
        if (vkres != VK_SUCCESS)
        {
            return vkres;
        }
    }

    // Framebuffer
    {
        std::vector<VkFormat> formats;
        for (uint32_t i = 0; i < CountU32(colorInfos); ++i)
        {
            formats.push_back(colorInfos[i].Format);
        }
        if (hasDepthStencil)
        {
            formats.push_back(depthStencilInfo.Format);
        }

        std::vector<VkFramebufferAttachmentImageInfo> attachmentImageInfos;
        for (uint32_t i = 0; i < CountU32(colorInfos); ++i)
        {
            VkFramebufferAttachmentImageInfo attachmentImageInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO};
            attachmentImageInfo.pNext                            = nullptr;
            attachmentImageInfo.flags                            = 0;
            attachmentImageInfo.usage                            = colorInfos[i].ImageUsage;
            attachmentImageInfo.width                            = width;
            attachmentImageInfo.height                           = height;
            attachmentImageInfo.layerCount                       = 1;
            attachmentImageInfo.viewFormatCount                  = 1;
            attachmentImageInfo.pViewFormats                     = &formats[i];
            attachmentImageInfos.push_back(attachmentImageInfo);
        }
        if (hasDepthStencil)
        {
            VkFramebufferAttachmentImageInfo attachmentImageInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO};
            attachmentImageInfo.pNext                            = nullptr;
            attachmentImageInfo.flags                            = 0;
            attachmentImageInfo.usage                            = depthStencilInfo.ImageUsage;
            attachmentImageInfo.width                            = width;
            attachmentImageInfo.height                           = height;
            attachmentImageInfo.layerCount                       = 1;
            attachmentImageInfo.viewFormatCount                  = 1;
            attachmentImageInfo.pViewFormats                     = &formats[formats.size() - 1];
            attachmentImageInfos.push_back(attachmentImageInfo);
        }

        VkFramebufferAttachmentsCreateInfo attachmentCreateInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO};
        attachmentCreateInfo.attachmentImageInfoCount           = CountU32(attachmentImageInfos);
        attachmentCreateInfo.pAttachmentImageInfos              = DataPtr(attachmentImageInfos);

        VkFramebufferCreateInfo createInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        createInfo.pNext                   = &attachmentCreateInfo;
        createInfo.flags                   = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
        createInfo.renderPass              = pRenderPass->RenderPass;
        createInfo.attachmentCount         = CountU32(colorInfos) + (hasDepthStencil ? 1 : 0);
        createInfo.width                   = width;
        createInfo.height                  = height;
        createInfo.layers                  = 1;

        VkResult vkres = vkCreateFramebuffer(
            pRenderer->Device,
            &createInfo,
            nullptr,
            &pRenderPass->Framebuffer);
        if (vkres != VK_SUCCESS)
        {
            return vkres;
        }
    }

    return VK_SUCCESS;
}

void DestroyBuffer(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer)
{
    vmaDestroyBuffer(pRenderer->Allocator, pBuffer->Buffer, pBuffer->Allocation);
    *pBuffer = {};
}

VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, const VulkanBuffer* pBuffer)
{
    assert((pBuffer != nullptr) && "pBuffer is NULL");

    VkBufferDeviceAddressInfo address_info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address_info.pNext                     = nullptr;
    address_info.buffer                    = pBuffer->Buffer;

    VkDeviceAddress address = vkGetBufferDeviceAddress(
        pRenderer->Device,
        &address_info);

    return address;
}

VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, VkAccelerationStructureKHR accelStruct)
{
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.pNext                                       = nullptr;
    addressInfo.accelerationStructure                       = accelStruct;

    VkDeviceAddress address = fn_vkGetAccelerationStructureDeviceAddressKHR(
        pRenderer->Device,
        &addressInfo);

    return address;
}

VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, const VulkanAccelStruct* pAccelStruct)
{
    assert((pAccelStruct != nullptr) && "pAccelStruct is NULL");
    return GetDeviceAddress(pRenderer, pAccelStruct->AccelStruct);
}

VkResult CreateDrawVertexColorPipeline(
    VulkanRenderer*     pRenderer,
    VkPipelineLayout    pipeline_layout,
    VkShaderModule      vsShaderModule,
    VkShaderModule      fsShaderModule,
    VkFormat            rtvFormat,
    VkFormat            dsvFormat,
    VkPipeline*         pPipeline,
    VkCullModeFlags     cullMode,
    VkPrimitiveTopology topologyType,
    uint32_t            pipelineFlags,
    const char*         vsEntryPoint,
    const char*         fsEntryPoint)
{
    bool                          isInterleavedAttrs             = pipelineFlags & VK_PIPELINE_FLAGS_INTERLEAVED_ATTRS;
    VkFormat                      rtv_format                     = GREX_DEFAULT_RTV_FORMAT;
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtv_format;
    pipeline_rendering_create_info.depthAttachmentFormat         = GREX_DEFAULT_DSV_FORMAT;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = vsEntryPoint;
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = fsEntryPoint;

    VkVertexInputBindingDescription vertex_binding_desc[2] = {};
    vertex_binding_desc[0].binding                         = 0;
    vertex_binding_desc[0].stride                          = isInterleavedAttrs ? 24 : 12;
    vertex_binding_desc[0].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

    vertex_binding_desc[1].binding   = 1;
    vertex_binding_desc[1].stride    = isInterleavedAttrs ? 24 : 12;
    vertex_binding_desc[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute_desc[2] = {};
    vertex_attribute_desc[0].location                          = 0;
    vertex_attribute_desc[0].binding                           = 0;
    vertex_attribute_desc[0].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset                            = 0;

    vertex_attribute_desc[1].location = 1;
    vertex_attribute_desc[1].binding  = isInterleavedAttrs ? 0 : 1;
    vertex_attribute_desc[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[1].offset   = isInterleavedAttrs ? 12 : 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state.vertexBindingDescriptionCount        = 2;
    vertex_input_state.pVertexBindingDescriptions           = vertex_binding_desc;
    vertex_input_state.vertexAttributeDescriptionCount      = 2;
    vertex_input_state.pVertexAttributeDescriptions         = vertex_attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology                               = topologyType;

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 1.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    VkDynamicState dynamic_states[2] = {};
    dynamic_states[0]                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = 2;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipeline_layout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateDrawNormalPipeline(
    VulkanRenderer*    pRenderer,
    VkPipelineLayout   pipelineLayout,
    VkShaderModule     vsShaderModule,
    VkShaderModule     fsShaderModule,
    VkFormat           rtvFormat,
    VkFormat           dsvFormat,
    VkPipeline*        pPipeline,
    bool               enableTangents,
    VkCullModeFlagBits cullMode,
    const char*        vsEntryPoint,
    const char*        fsEntryPoint)
{
    VkFormat                      rtv_format                     = GREX_DEFAULT_RTV_FORMAT;
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtv_format;
    pipeline_rendering_create_info.depthAttachmentFormat         = GREX_DEFAULT_DSV_FORMAT;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = vsEntryPoint;
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = fsEntryPoint;

    VkVertexInputBindingDescription vertex_binding_desc[4] = {};
    vertex_binding_desc[0].binding                         = 0;
    vertex_binding_desc[0].stride                          = 12;
    vertex_binding_desc[0].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

    vertex_binding_desc[1].binding   = 1;
    vertex_binding_desc[1].stride    = 12;
    vertex_binding_desc[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    if (enableTangents)
    {
        vertex_binding_desc[2].binding   = 2;
        vertex_binding_desc[2].stride    = 12;
        vertex_binding_desc[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        vertex_binding_desc[3].binding   = 3;
        vertex_binding_desc[3].stride    = 12;
        vertex_binding_desc[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    }

    VkVertexInputAttributeDescription vertex_attribute_desc[4] = {};
    vertex_attribute_desc[0].location                          = 0;
    vertex_attribute_desc[0].binding                           = 0;
    vertex_attribute_desc[0].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset                            = 0;

    vertex_attribute_desc[1].location = 1;
    vertex_attribute_desc[1].binding  = 1;
    vertex_attribute_desc[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[1].offset   = 0;

    if (enableTangents)
    {
        vertex_attribute_desc[2].location = 2;
        vertex_attribute_desc[2].binding  = 2;
        vertex_attribute_desc[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attribute_desc[2].offset   = 0;

        vertex_attribute_desc[3].location = 3;
        vertex_attribute_desc[3].binding  = 3;
        vertex_attribute_desc[3].format   = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attribute_desc[3].offset   = 0;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state.vertexBindingDescriptionCount        = enableTangents ? 4 : 2;
    vertex_input_state.pVertexBindingDescriptions           = vertex_binding_desc;
    vertex_input_state.vertexAttributeDescriptionCount      = enableTangents ? 4 : 2;
    vertex_input_state.pVertexAttributeDescriptions         = vertex_attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 1.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    VkDynamicState dynamic_states[2] = {};
    dynamic_states[0]                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = 2;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipelineLayout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateDrawTexturePipeline(
    VulkanRenderer*    pRenderer,
    VkPipelineLayout   pipelineLayout,
    VkShaderModule     vsShaderModule,
    VkShaderModule     fsShaderModule,
    VkFormat           rtvFormat,
    VkFormat           dsvFormat,
    VkPipeline*        pPipeline,
    VkCullModeFlagBits cullMode,
    const char*        vsEntryPoint,
    const char*        fsEntryPoint)
{
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtvFormat;
    pipeline_rendering_create_info.depthAttachmentFormat         = GREX_DEFAULT_DSV_FORMAT;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = vsEntryPoint;
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = fsEntryPoint;

    VkVertexInputBindingDescription vertex_binding_desc[2] = {};
    vertex_binding_desc[0].binding                         = 0;
    vertex_binding_desc[0].stride                          = 12;
    vertex_binding_desc[0].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

    vertex_binding_desc[1].binding   = 1;
    vertex_binding_desc[1].stride    = 8;
    vertex_binding_desc[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute_desc[2] = {};
    vertex_attribute_desc[0].location                          = 0;
    vertex_attribute_desc[0].binding                           = 0;
    vertex_attribute_desc[0].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset                            = 0;

    vertex_attribute_desc[1].location = 1;
    vertex_attribute_desc[1].binding  = 1;
    vertex_attribute_desc[1].format   = VK_FORMAT_R32G32_SFLOAT;
    vertex_attribute_desc[1].offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state.vertexBindingDescriptionCount        = 2;
    vertex_input_state.pVertexBindingDescriptions           = vertex_binding_desc;
    vertex_input_state.vertexAttributeDescriptionCount      = 2;
    vertex_input_state.pVertexAttributeDescriptions         = vertex_attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 1.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    VkDynamicState dynamic_states[2] = {};
    dynamic_states[0]                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = 2;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipelineLayout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateDrawBasicPipeline(
    VulkanRenderer*    pRenderer,
    VkPipelineLayout   pipelineLayout,
    VkShaderModule     vsShaderModule,
    VkShaderModule     fsShaderModule,
    VkFormat           rtvFormat,
    VkFormat           dsvFormat,
    VkPipeline*        pPipeline,
    VkCullModeFlagBits cullMode,
    const char*        vsEntryPoint,
    const char*        fsEntryPoint)
{
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtvFormat;
    pipeline_rendering_create_info.depthAttachmentFormat         = GREX_DEFAULT_DSV_FORMAT;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = vsEntryPoint;
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = fsEntryPoint;

    VkVertexInputBindingDescription vertex_binding_desc[3] = {};
    vertex_binding_desc[0].binding                         = 0;
    vertex_binding_desc[0].stride                          = 12;
    vertex_binding_desc[0].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;
    vertex_binding_desc[1].binding                         = 1;
    vertex_binding_desc[1].stride                          = 8;
    vertex_binding_desc[1].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;
    vertex_binding_desc[2].binding                         = 2;
    vertex_binding_desc[2].stride                          = 12;
    vertex_binding_desc[2].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute_desc[3] = {};
    vertex_attribute_desc[0].location                          = 0;
    vertex_attribute_desc[0].binding                           = 0;
    vertex_attribute_desc[0].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset                            = 0;
    vertex_attribute_desc[1].location                          = 1;
    vertex_attribute_desc[1].binding                           = 1;
    vertex_attribute_desc[1].format                            = VK_FORMAT_R32G32_SFLOAT;
    vertex_attribute_desc[1].offset                            = 0;
    vertex_attribute_desc[2].location                          = 2;
    vertex_attribute_desc[2].binding                           = 2;
    vertex_attribute_desc[2].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[2].offset                            = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state.vertexBindingDescriptionCount        = 3;
    vertex_input_state.pVertexBindingDescriptions           = vertex_binding_desc;
    vertex_input_state.vertexAttributeDescriptionCount      = 3;
    vertex_input_state.pVertexAttributeDescriptions         = vertex_attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 1.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    VkDynamicState dynamic_states[2] = {};
    dynamic_states[0]                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = 2;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipelineLayout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateGraphicsPipeline1(
    VulkanRenderer*    pRenderer,
    VkPipelineLayout   pipelineLayout,
    VkShaderModule     vsShaderModule,
    VkShaderModule     fsShaderModule,
    VkFormat           rtvFormat,
    VkFormat           dsvFormat,
    VkPipeline*        pPipeline,
    VkCullModeFlagBits cullMode)
{
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtvFormat;
    pipeline_rendering_create_info.depthAttachmentFormat         = dsvFormat;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = "vsmain";
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = "psmain";

    const uint32_t kNumInputElements = 5;

    VkVertexInputBindingDescription vertex_binding_desc[kNumInputElements] = {};
    // Position
    vertex_binding_desc[0].binding   = 0;
    vertex_binding_desc[0].stride    = 12;
    vertex_binding_desc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // TexCoord
    vertex_binding_desc[1].binding   = 1;
    vertex_binding_desc[1].stride    = 8;
    vertex_binding_desc[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // Normal
    vertex_binding_desc[2].binding   = 2;
    vertex_binding_desc[2].stride    = 12;
    vertex_binding_desc[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // Tangent
    vertex_binding_desc[3].binding   = 3;
    vertex_binding_desc[3].stride    = 12;
    vertex_binding_desc[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // Bitangent
    vertex_binding_desc[4].binding   = 4;
    vertex_binding_desc[4].stride    = 12;
    vertex_binding_desc[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute_desc[kNumInputElements] = {};
    // Position
    vertex_attribute_desc[0].location = 0;
    vertex_attribute_desc[0].binding  = 0;
    vertex_attribute_desc[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset   = 0;
    // TexCoord
    vertex_attribute_desc[1].location = 1;
    vertex_attribute_desc[1].binding  = 1;
    vertex_attribute_desc[1].format   = VK_FORMAT_R32G32_SFLOAT;
    vertex_attribute_desc[1].offset   = 0;
    // Normal
    vertex_attribute_desc[2].location = 2;
    vertex_attribute_desc[2].binding  = 2;
    vertex_attribute_desc[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[2].offset   = 0;
    // Tangent
    vertex_attribute_desc[3].location = 3;
    vertex_attribute_desc[3].binding  = 3;
    vertex_attribute_desc[3].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[3].offset   = 0;
    // Bitangent
    vertex_attribute_desc[4].location = 4;
    vertex_attribute_desc[4].binding  = 4;
    vertex_attribute_desc[4].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[4].offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state.vertexBindingDescriptionCount        = kNumInputElements;
    vertex_input_state.pVertexBindingDescriptions           = vertex_binding_desc;
    vertex_input_state.vertexAttributeDescriptionCount      = kNumInputElements;
    vertex_input_state.pVertexAttributeDescriptions         = vertex_attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 1.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    VkDynamicState dynamic_states[2] = {};
    dynamic_states[0]                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = 2;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipelineLayout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateGraphicsPipeline2(
    VulkanRenderer*    pRenderer,
    VkPipelineLayout   pipelineLayout,
    VkShaderModule     vsShaderModule,
    VkShaderModule     fsShaderModule,
    VkFormat           rtvFormat,
    VkFormat           dsvFormat,
    VkPipeline*        pPipeline,
    VkCullModeFlagBits cullMode)
{
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtvFormat;
    pipeline_rendering_create_info.depthAttachmentFormat         = dsvFormat;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = "vsmain";
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = "psmain";

    const uint32_t kNumInputElements = 4;

    VkVertexInputBindingDescription vertex_binding_desc[kNumInputElements] = {};
    // Position
    vertex_binding_desc[0].binding   = 0;
    vertex_binding_desc[0].stride    = 12;
    vertex_binding_desc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // TexCoord
    vertex_binding_desc[1].binding   = 1;
    vertex_binding_desc[1].stride    = 8;
    vertex_binding_desc[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // Normal
    vertex_binding_desc[2].binding   = 2;
    vertex_binding_desc[2].stride    = 12;
    vertex_binding_desc[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // Tangent
    vertex_binding_desc[3].binding   = 3;
    vertex_binding_desc[3].stride    = 16;
    vertex_binding_desc[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute_desc[kNumInputElements] = {};
    // Position
    vertex_attribute_desc[0].location = 0;
    vertex_attribute_desc[0].binding  = 0;
    vertex_attribute_desc[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset   = 0;
    // TexCoord
    vertex_attribute_desc[1].location = 1;
    vertex_attribute_desc[1].binding  = 1;
    vertex_attribute_desc[1].format   = VK_FORMAT_R32G32_SFLOAT;
    vertex_attribute_desc[1].offset   = 0;
    // Normal
    vertex_attribute_desc[2].location = 2;
    vertex_attribute_desc[2].binding  = 2;
    vertex_attribute_desc[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[2].offset   = 0;
    // Tangent
    vertex_attribute_desc[3].location = 3;
    vertex_attribute_desc[3].binding  = 3;
    vertex_attribute_desc[3].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attribute_desc[3].offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_state.vertexBindingDescriptionCount        = kNumInputElements;
    vertex_input_state.pVertexBindingDescriptions           = vertex_binding_desc;
    vertex_input_state.vertexAttributeDescriptionCount      = kNumInputElements;
    vertex_input_state.pVertexAttributeDescriptions         = vertex_attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 1.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    const int      dynamicStateCount                 = 3;
    VkDynamicState dynamic_states[dynamicStateCount] = {};
    dynamic_states[0]                                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                                = VK_DYNAMIC_STATE_SCISSOR;
    dynamic_states[2]                                = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = dynamicStateCount;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipelineLayout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateMeshShaderPipeline(
    VulkanRenderer*     pRenderer,
    VkPipelineLayout    pipelineLayout,
    VkShaderModule      asShaderModule,
    VkShaderModule      msShaderModule,
    VkShaderModule      fsShaderModule,
    VkFormat            rtvFormat,
    VkFormat            dsvFormat,
    VkPipeline*         pPipeline,
    VkCullModeFlags     cullMode,
    VkPrimitiveTopology topologyType,
    uint32_t            pipelineFlags)
{
    bool                          isInterleavedAttrs             = pipelineFlags & VK_PIPELINE_FLAGS_INTERLEAVED_ATTRS;
    VkFormat                      rtv_format                     = GREX_DEFAULT_RTV_FORMAT;
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtv_format;
    pipeline_rendering_create_info.depthAttachmentFormat         = GREX_DEFAULT_DSV_FORMAT;

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    // Task (amplification) shader
    if (asShaderModule != VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        shaderStageCreateInfo.stage                           = VK_SHADER_STAGE_TASK_BIT_EXT;
        shaderStageCreateInfo.module                          = asShaderModule;
        shaderStageCreateInfo.pName                           = "asmain";
        shaderStages.push_back(shaderStageCreateInfo);
    }
    // Mesh shader
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStageCreateInfo.stage                           = VK_SHADER_STAGE_MESH_BIT_EXT;
    shaderStageCreateInfo.module                          = msShaderModule;
    shaderStageCreateInfo.pName                           = "msmain";
    shaderStages.push_back(shaderStageCreateInfo);
    // Fragment shader
    shaderStageCreateInfo        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStageCreateInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStageCreateInfo.module = fsShaderModule;
    shaderStageCreateInfo.pName  = "psmain";
    shaderStages.push_back(shaderStageCreateInfo);

    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization_state.depthClampEnable                       = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable                = VK_FALSE;
    rasterization_state.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                               = cullMode;
    rasterization_state.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable                        = VK_TRUE;
    rasterization_state.depthBiasConstantFactor                = 0.0f;
    rasterization_state.depthBiasClamp                         = 0.0f;
    rasterization_state.depthBiasSlopeFactor                   = 0.0f;
    rasterization_state.lineWidth                              = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthTestEnable                       = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthWriteEnable                      = (dsvFormat != VK_FORMAT_UNDEFINED);
    depth_stencil_state.depthCompareOp                        = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable                 = VK_FALSE;
    depth_stencil_state.stencilTestEnable                     = VK_FALSE;
    depth_stencil_state.front.failOp                          = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.depthFailOp                     = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.compareOp                       = VK_COMPARE_OP_NEVER;
    depth_stencil_state.back                                  = depth_stencil_state.front;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.blendEnable                         = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_COLOR;
    color_blend_attachment_state.dstColorBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp                        = VK_BLEND_OP_ADD;
    color_blend_attachment_state.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state.logicOpEnable                       = VK_FALSE;
    color_blend_state.logicOp                             = VK_LOGIC_OP_NO_OP;
    color_blend_state.attachmentCount                     = 1;
    color_blend_state.pAttachments                        = &color_blend_attachment_state;
    color_blend_state.blendConstants[0]                   = 0.0f;
    color_blend_state.blendConstants[1]                   = 0.0f;
    color_blend_state.blendConstants[2]                   = 0.0f;
    color_blend_state.blendConstants[3]                   = 0.0f;

    VkDynamicState dynamic_states[2] = {};
    dynamic_states[0]                = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1]                = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount                = 2;
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineMultisampleStateCreateInfo pipelineMultiStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    pipelineMultiStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = CountU32(shaderStages);
    pipeline_info.pStages                      = DataPtr(shaderStages);
    pipeline_info.pVertexInputState            = nullptr;
    pipeline_info.pInputAssemblyState          = nullptr;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
    pipeline_info.pMultisampleState            = &pipelineMultiStateCreateInfo;
    pipeline_info.pDepthStencilState           = &depth_stencil_state;
    pipeline_info.pColorBlendState             = &color_blend_state;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipelineLayout;
    pipeline_info.renderPass                   = VK_NULL_HANDLE;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    VkResult vkres = vkCreateGraphicsPipelines(
        pRenderer->Device,
        VK_NULL_HANDLE, // Not using a pipeline cache
        1,
        &pipeline_info,
        nullptr,
        pPipeline);

    return vkres;
}

VkResult CreateMeshShaderPipeline(
    VulkanRenderer*     pRenderer,
    VkPipelineLayout    pipelineLayout,
    VkShaderModule      msShaderModule,
    VkShaderModule      fsShaderModule,
    VkFormat            rtvFormat,
    VkFormat            dsvFormat,
    VkPipeline*         pPipeline,
    VkCullModeFlags     cullMode,
    VkPrimitiveTopology topologyType,
    uint32_t            pipelineFlags)
{
    return CreateMeshShaderPipeline(
        pRenderer,      // pRenderer
        pipelineLayout, // pipelineLayout
        VK_NULL_HANDLE, // asShaderModul,
        msShaderModule, // msShaderModule
        fsShaderModule, // fsShaderModule
        rtvFormat,      // rtvFormat
        dsvFormat,      // dsvFormat
        pPipeline,      // pPipeline
        cullMode,       // cullMode
        topologyType,   // topologyType
        pipelineFlags); // pipelineFlags
}

CompileResult CompileGLSL(
    const std::string&     shaderSource,
    VkShaderStageFlagBits  shaderStage,
    const CompilerOptions& options,
    std::vector<uint32_t>* pSPIRV,
    std::string*           pErrorMsg)
{
    const glslang_stage_t                 k_invalid_stage  = static_cast<glslang_stage_t>(~0);
    const glslang_target_client_version_t k_client_version = GLSLANG_TARGET_VULKAN_1_3;

    glslang_stage_t glslang_stage = k_invalid_stage;
    // clang-format off
    switch (shaderStage) {
        default: break;
        case VK_SHADER_STAGE_VERTEX_BIT                  : glslang_stage = GLSLANG_STAGE_VERTEX; break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT    : glslang_stage = GLSLANG_STAGE_TESSCONTROL; break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : glslang_stage = GLSLANG_STAGE_TESSEVALUATION; break;
        case VK_SHADER_STAGE_GEOMETRY_BIT                : glslang_stage = GLSLANG_STAGE_GEOMETRY; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT                : glslang_stage = GLSLANG_STAGE_FRAGMENT; break;
        case VK_SHADER_STAGE_COMPUTE_BIT                 : glslang_stage = GLSLANG_STAGE_COMPUTE; break;
        case VK_SHADER_STAGE_RAYGEN_BIT_KHR              : glslang_stage = GLSLANG_STAGE_RAYGEN_NV; break;
        case VK_SHADER_STAGE_ANY_HIT_BIT_KHR             : glslang_stage = GLSLANG_STAGE_ANYHIT_NV; break;
        case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR         : glslang_stage = GLSLANG_STAGE_CLOSESTHIT_NV; break;
        case VK_SHADER_STAGE_MISS_BIT_KHR                : glslang_stage = GLSLANG_STAGE_MISS_NV; break;
        case VK_SHADER_STAGE_INTERSECTION_BIT_KHR        : glslang_stage = GLSLANG_STAGE_INTERSECT_NV; break;
        case VK_SHADER_STAGE_CALLABLE_BIT_KHR            : glslang_stage = GLSLANG_STAGE_CALLABLE_NV; break;
        case VK_SHADER_STAGE_TASK_BIT_EXT                : glslang_stage = GLSLANG_STAGE_MESH_NV; break;
        case VK_SHADER_STAGE_MESH_BIT_EXT                : glslang_stage = GLSLANG_STAGE_TASK_NV; break;
    }
    // clang-format on
    if (glslang_stage == k_invalid_stage)
    {
        return COMPILE_ERROR_INVALID_SHADER_STAGE;
    }

    glslang_input_t input                   = {};
    input.language                          = GLSLANG_SOURCE_GLSL;
    input.stage                             = glslang_stage;
    input.client                            = GLSLANG_CLIENT_VULKAN;
    input.client_version                    = k_client_version;
    input.target_language                   = GLSLANG_TARGET_SPV;
    input.target_language_version           = GLSLANG_TARGET_SPV_1_4;
    input.code                              = shaderSource.c_str();
    input.default_version                   = 100;
    input.default_profile                   = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible                = false;
    input.messages                          = GLSLANG_MSG_DEFAULT_BIT;
    input.resource                          = glslang_default_resource();

    int res = glslang_initialize_process();
    if (res == 0)
    {
        return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
    }

    struct ScopedShader
    {
        glslang_shader_t* pObject = nullptr;

        operator glslang_shader_t*() const
        {
            return pObject;
        }

        ~ScopedShader()
        {
            if (!IsNull(pObject))
            {
                glslang_shader_delete(pObject);
                pObject = nullptr;
            }
        }
    };

    struct ScopedProgram
    {
        glslang_program_t* pObject = nullptr;

        operator glslang_program_t*() const
        {
            return pObject;
        }

        ~ScopedProgram()
        {
            if (!IsNull(pObject))
            {
                glslang_program_delete(pObject);
                pObject = nullptr;
            }
        }
    };

    ScopedShader shader = {};
    {
        glslang_shader_t* p_shader = glslang_shader_create(&input);

        if (IsNull(p_shader))
        {
            return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
        }
        shader.pObject = p_shader;
    }

    //
    // Shift registers
    //
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_TEXTURE, options.BindingShiftTexture);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_UBO, options.BindingShiftUBO);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_IMAGE, options.BindingShiftImage);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_SAMPLER, options.BindingShiftSampler);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_SSBO, options.BindingShiftSSBO);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_UAV, options.BindingShiftUAV);

    //
    // glslang Options
    //
    int shader_options = GLSLANG_SHADER_AUTO_MAP_BINDINGS |
                         GLSLANG_SHADER_AUTO_MAP_LOCATIONS |
                         GLSLANG_SHADER_VULKAN_RULES_RELAXED;
    glslang_shader_set_options(shader, shader_options);

    //
    // Preprocess
    //
    if (!glslang_shader_preprocess(shader, &input))
    {
        std::stringstream ss;

        const char* infoLog = glslang_shader_get_info_log(shader);
        if (infoLog != nullptr)
        {
            ss << "GLSL preprocess failed (info): " << infoLog;
        }

        const char* debugLog = glslang_shader_get_info_debug_log(shader);
        if (debugLog != nullptr)
        {
            ss << "GLSL preprocess failed (debug): " << debugLog;
        }

        if (!IsNull(pErrorMsg))
        {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_PREPROCESS_FAILED;
    }

    //
    // Compile
    //
    if (!glslang_shader_parse(shader, &input))
    {
        std::stringstream ss;

        const char* info_log = glslang_shader_get_info_log(shader);
        if (info_log != nullptr)
        {
            ss << "GLSL compile failed (info): " << info_log;
        }

        const char* debug_log = glslang_shader_get_info_debug_log(shader);
        if (debug_log != nullptr)
        {
            ss << "GLSL compile failed (debug): " << debug_log;
        }

        if (!IsNull(pErrorMsg))
        {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_COMPILE_FAILED;
    }

    //
    // Link
    //
    ScopedProgram program = {};
    {
        glslang_program_t* p_program = glslang_program_create();
        if (IsNull(p_program))
        {
            return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
        }
        program.pObject = p_program;
    }
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT))
    {
        std::stringstream ss;

        const char* info_log = glslang_program_get_info_log(program);
        if (info_log != nullptr)
        {
            ss << "GLSL link failed (info): " << info_log;
        }

        const char* debug_log = glslang_program_get_info_debug_log(program);
        if (debug_log != nullptr)
        {
            ss << "GLSL link failed (debug): " << debug_log;
        }

        if (!IsNull(pErrorMsg))
        {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_LINK_FAILED;
    }

    //
    // Map IO
    //
    if (!glslang_program_map_io(program))
    {
        std::stringstream ss;

        ss << "GLSL program map IO failed";

        if (!IsNull(pErrorMsg))
        {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_MAP_IO_FAILED;
    }

    //
    // Get SPIR-V
    //
    if (!IsNull(pSPIRV))
    {
        glslang_program_SPIRV_generate(program, input.stage);
        const char* spirv_msg = glslang_program_SPIRV_get_messages(program);
        if (!IsNull(spirv_msg))
        {
            std::stringstream ss;
            ss << "SPIR-V generation error: " << spirv_msg;

            if (!IsNull(pErrorMsg))
            {
                *pErrorMsg = ss.str();
            }

            return COMPILE_ERROR_CODE_GEN_FAILED;
        }

        const size_t    size    = glslang_program_SPIRV_get_size(program);
        const uint32_t* p_spirv = reinterpret_cast<const uint32_t*>(glslang_program_SPIRV_get_ptr(program));

        *pSPIRV = std::vector<uint32_t>(p_spirv, p_spirv + size);
    }

    //
    // Finish
    //
    glslang_finalize_process();

    return COMPILE_SUCCESS;
}

static std::wstring AsciiToUTF16(const std::string& ascii)
{
    std::wstring utf16;
    for (auto& c : ascii)
    {
        utf16.push_back(static_cast<std::wstring::value_type>(c));
    }
    return utf16;
}

HRESULT CompileHLSL(
    const std::string&     shaderSource,
    const std::string&     entryPoint,
    const std::string&     profile,
    std::vector<uint32_t>* pSPIRV,
    std::string*           pErrorMsg)
{
    // Check source
    if (shaderSource.empty())
    {
        assert(false && "no shader source");
        return E_INVALIDARG;
    }
    // Check entry point
    if (entryPoint.empty() && (!profile.starts_with("lib_6_")))
    {
        assert(false && "no entrypoint");
        return E_INVALIDARG;
    }
    // Check profile
    if (profile.empty())
    {
        assert(false && "no profile");
        return E_INVALIDARG;
    }
    // Check output
    if (IsNull(pSPIRV))
    {
        assert(false && "DXIL output arg is null");
        return E_INVALIDARG;
    }

    ComPtr<IDxcLibrary> dxcLibrary;
    HRESULT             hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&dxcLibrary));

    ComPtr<IDxcCompiler3> dxcCompiler;
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));

    DxcBuffer source = {};
    source.Ptr       = shaderSource.data();
    source.Size      = shaderSource.length();

    std::wstring entryPointUTF16 = AsciiToUTF16(entryPoint);
    std::wstring profileUT16     = AsciiToUTF16(profile);

    std::vector<LPCWSTR> args = {
        L"-spirv",
        L"-fspv-target-env=vulkan1.1spirv1.4",
        L"-fvk-use-dx-layout"};

    args.push_back(L"-E");
    args.push_back(entryPointUTF16.c_str());
    args.push_back(L"-T");
    args.push_back(profileUT16.c_str());

    ComPtr<IDxcResult> result;
    hr = dxcCompiler->Compile(
        &source,
        args.data(),
        static_cast<UINT>(args.size()),
        nullptr,
        IID_PPV_ARGS(&result));
    if (FAILED(hr))
    {
        assert(false && "compile failed");
        return hr;
    }

    ComPtr<IDxcBlob> errors;
    hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (FAILED(hr))
    {
        assert(false && "Get error output failed");
        return hr;
    }
    if (errors && (errors->GetBufferSize() > 0) && !IsNull(pErrorMsg))
    {
        const char* pBuffer    = static_cast<const char*>(errors->GetBufferPointer());
        size_t      bufferSize = static_cast<size_t>(errors->GetBufferSize());
        *pErrorMsg             = std::string(pBuffer, pBuffer + bufferSize);
        return E_FAIL;
    }

    ComPtr<IDxcBlob> shaderBinary;
    hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBinary), nullptr);
    if (FAILED(hr))
    {
        assert(false && "Get compile output failed");
        return hr;
    }

    const char* pBuffer    = static_cast<const char*>(shaderBinary->GetBufferPointer());
    size_t      bufferSize = static_cast<size_t>(shaderBinary->GetBufferSize());
    size_t      wordCount  = bufferSize / 4;

    pSPIRV->resize(wordCount);
    memcpy(pSPIRV->data(), pBuffer, bufferSize);

    return S_OK;
}

#if defined(GREX_ENABLE_SLANG)
CompileResult CompileSlang(
    const std::string&     shaderSource,
    const std::string&     entryPoint,
    const std::string&     profile,
    const CompilerOptions& options,
    std::vector<uint32_t>* pSPIRV,
    std::string*           pErrorMsg)
{
    // Bail if entry point is empty and we're not compiling to a library
    bool isTargetLibrary = profile.starts_with("lib_6_");
    if (!isTargetLibrary && entryPoint.empty())
    {
        return COMPILE_ERROR_INVALID_ENTRY_POINT;
    }

    Slang::ComPtr<slang::IGlobalSession> globalSession;
    if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
    {
        return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
    }

    slang::TargetDesc targetDesc = {};
    targetDesc.format            = SLANG_SPIRV;
    targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
    //
    // Disable profile for now since it's causing ISession::loadModuleFromSourceString to crash.
    //
    //targetDesc.profile           = globalSession->findProfile(profile.c_str());

    targetDesc.flags |= SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM;

    // Must be set in target desc for now
    targetDesc.forceGLSLScalarBufferLayout = true;

    // Compiler options for Slang
    std::vector<slang::CompilerOptionEntry> compilerOptions;
    {
        // Force Slang language to prevent any accidental interpretations as GLSL or HLSL
        {
            slang::CompilerOptionEntry entry = {slang::CompilerOptionName::Language};
            entry.value.stringValue0         = "slang";

            compilerOptions.push_back(entry);
        }

        // Force "main" entry point if requested
        if (!options.ForceEntryPointMain)
        {
            compilerOptions.push_back(
                slang::CompilerOptionEntry{
                    slang::CompilerOptionName::VulkanUseEntryPointName,
                    slang::CompilerOptionValue{slang::CompilerOptionValueKind::Int, 1}
            });
        }

        // Force scalar block layout - this gets overwritten by forceGLSLScalarBufferLayout in
        // the target desc currently. So we just set it there.
        //
        {
            compilerOptions.push_back(
                slang::CompilerOptionEntry{
                    slang::CompilerOptionName::GLSLForceScalarLayout,
                    slang::CompilerOptionValue{slang::CompilerOptionValueKind::Int, 1}
            });
        }
    }

    slang::SessionDesc sessionDesc       = {};
    sessionDesc.targets                  = &targetDesc;
    sessionDesc.targetCount              = 1;
    sessionDesc.defaultMatrixLayoutMode  = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    sessionDesc.compilerOptionEntries    = compilerOptions.data();
    sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptions.size());

    Slang::ComPtr<slang::ISession> compileSession;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, compileSession.writeRef())))
    {
        return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
    }

    // Load source
    slang::IModule* pSlangModule = nullptr;
    {
        Slang::ComPtr<slang::IBlob> diagBlob;

        pSlangModule = compileSession->loadModuleFromSourceString("grex-module", "grex-path", shaderSource.c_str(), diagBlob.writeRef());
        if (pSlangModule == nullptr)
        {
            if (pErrorMsg != nullptr)
            {
                *pErrorMsg = std::string(static_cast<const char*>(diagBlob->getBufferPointer()), diagBlob->getBufferSize());
            }

            return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
        }
    }

    Slang::ComPtr<slang::IBlob> spirvCode;
    if (isTargetLibrary)
    {
        //
        // NOTE: This may not be the most correct way to do it, but it works for now
        //

        // Create compile request
        std::unique_ptr<SlangCompileRequest, void (*)(SlangCompileRequest*)> compileRequest(nullptr, nullptr);
        {
            SlangCompileRequest* pCompileRequest = nullptr;
            auto                 slangRes        = compileSession->createCompileRequest(&pCompileRequest);
            if (SLANG_FAILED(slangRes))
            {
                return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
            }

            compileRequest = std::unique_ptr<SlangCompileRequest, void (*)(SlangCompileRequest*)>(pCompileRequest, spDestroyCompileRequest);
        }

        // Add translation unit
        auto index = compileRequest->addTranslationUnit(SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
        compileRequest->addTranslationUnitSourceString(index, "grex-path", shaderSource.c_str());

        // Compile
        auto slangRes = compileRequest->compile();
        if (SLANG_FAILED(slangRes))
        {
            if (pErrorMsg != nullptr)
            {
                Slang::ComPtr<slang::IBlob> diagBlob;

                slangRes = compileRequest->getDiagnosticOutputBlob(diagBlob.writeRef());
                if (SLANG_SUCCEEDED(slangRes))
                {
                    *pErrorMsg = std::string(static_cast<const char*>(diagBlob->getBufferPointer()), diagBlob->getBufferSize());
                }
                else
                {
                    // Something has gone really wrong
                    assert(false && "failed to get diagnostic output blob");
                }
            }

            return COMPILE_ERROR_COMPILE_FAILED;
        }

        // Get SPIR-V
        slangRes = compileRequest->getTargetCodeBlob(0, spirvCode.writeRef());
        if (SLANG_FAILED(slangRes))
        {
            if (pErrorMsg != nullptr)
            {
                *pErrorMsg = "unable to retrieve SPIR-V blob for library";
            }

            return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
        }
    }
    else
    {
        // Load source
        slang::IModule* pSlangModule = nullptr;
        {
            Slang::ComPtr<slang::IBlob> diagBlob;

            pSlangModule = compileSession->loadModuleFromSourceString("grex-module", "", shaderSource.c_str(), diagBlob.writeRef());
            if (pSlangModule == nullptr)
            {
                if (pErrorMsg != nullptr)
                {
                    *pErrorMsg = std::string(static_cast<const char*>(diagBlob->getBufferPointer()), diagBlob->getBufferSize());
                }

                return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
            }
        }

        // Components
        std::vector<slang::IComponentType*> components;
        components.push_back(pSlangModule);

        // Entry points
        if (!entryPoint.empty())
        {
            Slang::ComPtr<slang::IEntryPoint> slangEntryPoint;
            if (SLANG_FAILED(pSlangModule->findEntryPointByName(entryPoint.c_str(), slangEntryPoint.writeRef())))
            {
                *pErrorMsg = "Couldn't find entry point by name";
                return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
            }
            components.push_back(slangEntryPoint);
        }
        else
        {
            SlangInt32 slangEntryPointCount = pSlangModule->getDefinedEntryPointCount();
            for (SlangInt32 i = 0; i < slangEntryPointCount; ++i)
            {
                ComPtr<slang::IEntryPoint> slangEntryPoint;
                if (SLANG_FAILED(pSlangModule->getDefinedEntryPoint(i, &slangEntryPoint)))
                {
                    *pErrorMsg = "Couldn't get defined entry point";
                    return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
                }
                components.push_back(slangEntryPoint.Get());
            }
        }

        Slang::ComPtr<slang::IComponentType> composedProgram;
        {
            Slang::ComPtr<slang::IBlob> diagBlob;

            auto slangRes = compileSession->createCompositeComponentType(
                components.data(),
                components.size(),
                composedProgram.writeRef(),
                diagBlob.writeRef());
            if (SLANG_FAILED(slangRes))
            {
                if (pErrorMsg != nullptr)
                {
                    *pErrorMsg = std::string(static_cast<const char*>(diagBlob->getBufferPointer()), diagBlob->getBufferSize());
                }

                return COMPILE_ERROR_COMPILE_FAILED;
            }
        }

        // Get SPIR-V
        {
            Slang::ComPtr<slang::IBlob> diagBlob;

            auto slangRes = composedProgram->getEntryPointCode(
                0, // entryPointIndex,
                0, // targetIndex,
                spirvCode.writeRef(),
                diagBlob.writeRef());
            if (SLANG_FAILED(slangRes))
            {
                if (pErrorMsg != nullptr)
                {
                    *pErrorMsg = std::string(static_cast<const char*>(diagBlob->getBufferPointer()), diagBlob->getBufferSize());
                }

                return COMPILE_ERROR_LINK_FAILED;
            }
        }
    }

    const char* pBuffer    = static_cast<const char*>(spirvCode->getBufferPointer());
    size_t      bufferSize = static_cast<size_t>(spirvCode->getBufferSize());
    size_t      wordCount  = bufferSize / 4;

    pSPIRV->resize(wordCount);
    memcpy(pSPIRV->data(), pBuffer, bufferSize);

    return COMPILE_SUCCESS;
}
#endif

void CreateDescriptor(
    VulkanRenderer*         pRenderer,
    VulkanBufferDescriptor* pBufferDescriptor,
    uint32_t                binding,
    uint32_t                arrayElement,
    VkDescriptorType        descriptorType,
    const VulkanBuffer*     pBuffer)
{
    assert(pBufferDescriptor);

    pBufferDescriptor->layoutBinding                 = {};
    pBufferDescriptor->layoutBinding.descriptorType  = descriptorType;
    pBufferDescriptor->layoutBinding.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS;
    pBufferDescriptor->layoutBinding.binding         = binding;
    pBufferDescriptor->layoutBinding.descriptorCount = 1;

    pBufferDescriptor->bufferInfo.buffer = pBuffer->Buffer;
    pBufferDescriptor->bufferInfo.offset = 0;
    pBufferDescriptor->bufferInfo.range  = VK_WHOLE_SIZE;

    pBufferDescriptor->writeDescriptorSet                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    pBufferDescriptor->writeDescriptorSet.descriptorType  = descriptorType;
    pBufferDescriptor->writeDescriptorSet.dstBinding      = binding;
    pBufferDescriptor->writeDescriptorSet.pBufferInfo     = &pBufferDescriptor->bufferInfo;
    pBufferDescriptor->writeDescriptorSet.descriptorCount = 1;
}

void WriteDescriptor(
    VulkanRenderer*       pRenderer,
    void*                 pDescriptorBufferStartAddress,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t              binding,
    uint32_t              arrayElement,
    VkDescriptorType      descriptorType,
    const VulkanBuffer*   pBuffer)
{
    // Get the descriptor buffer properties so we can look up the descriptor size
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2                   properties                 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext                                                         = &descriptorBufferProperties;
    vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

    // Address info
    VkDescriptorAddressInfoEXT addressInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
    addressInfo.address                    = GetDeviceAddress(pRenderer, pBuffer);
    addressInfo.range                      = pBuffer->Size;

    // Get buffer device address for acceleration structure
    VkDescriptorGetInfoEXT descriptorInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    descriptorInfo.type                   = descriptorType;

    // Set address info and figure out descriptor size
    VkDeviceSize descriptorSize = 0;
    switch (descriptorType)
    {
        default: break;

        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        {
            descriptorInfo.data.pUniformBuffer = &addressInfo;
            descriptorSize                     = static_cast<VkDeviceSize>(descriptorBufferProperties.uniformBufferDescriptorSize);
        }
        break;

        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        {
            descriptorInfo.data.pStorageBuffer = &addressInfo;
            descriptorSize                     = static_cast<VkDeviceSize>(descriptorBufferProperties.storageBufferDescriptorSize);
        }
        break;
    }

    // Get the offset for the binding
    VkDeviceSize bindingOffset = 0;
    fn_vkGetDescriptorSetLayoutBindingOffsetEXT(
        pRenderer->Device,
        descriptorSetLayout,
        binding,
        &bindingOffset);

    // Calculate array element offset
    VkDeviceSize arrayElementOffset = arrayElement * descriptorSize;

    // Descriptor location
    VkDeviceSize location = bindingOffset + arrayElementOffset;

    // Write the descriptor
    char* pDescriptor = static_cast<char*>(pDescriptorBufferStartAddress) + location;
    fn_vkGetDescriptorEXT(
        pRenderer->Device, // device
        &descriptorInfo,   // pDescriptorInfo
        descriptorSize,    // dataSize
        pDescriptor);      // pDescriptor
}

void WriteDescriptor(
    VulkanRenderer*          pRenderer,
    void*                    pDescriptorBufferStartAddress,
    VkDescriptorSetLayout    descriptorSetLayout,
    uint32_t                 binding,
    uint32_t                 arrayElement,
    const VulkanAccelStruct* pAccelStruct)
{
    // Get the descriptor buffer properties so we can look up the descriptor size
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2                   properties                 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext                                                         = &descriptorBufferProperties;
    vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

    // Get buffer device address for acceleration structure
    VkDescriptorGetInfoEXT descriptorInfo     = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    descriptorInfo.type                       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    descriptorInfo.data.accelerationStructure = GetDeviceAddress(pRenderer, pAccelStruct);

    // Descriptor size
    VkDeviceSize descriptorSize = static_cast<uint32_t>(descriptorBufferProperties.accelerationStructureDescriptorSize);

    // Get the offset for the binding
    VkDeviceSize bindingOffset = 0;
    fn_vkGetDescriptorSetLayoutBindingOffsetEXT(
        pRenderer->Device,
        descriptorSetLayout,
        binding,
        &bindingOffset);

    // Calculate array element offset
    VkDeviceSize arrayElementOffset = arrayElement * descriptorSize;

    // Descriptor location
    VkDeviceSize location = bindingOffset + arrayElementOffset;

    // Write the descriptor
    char* pDescriptor = static_cast<char*>(pDescriptorBufferStartAddress) + location;
    fn_vkGetDescriptorEXT(
        pRenderer->Device, // device
        &descriptorInfo,   // pDescriptorInfo
        descriptorSize,    // dataSize
        pDescriptor);      // pDescriptor
}

void CreateDescriptor(
    VulkanRenderer*        pRenderer,
    VulkanImageDescriptor* pImageDescriptor,
    uint32_t               binding,
    uint32_t               arrayElement,
    VkDescriptorType       descriptorType,
    VkImageView            imageView,
    VkImageLayout          imageLayout)
{
    assert(pImageDescriptor);

    pImageDescriptor->layoutBinding                 = {};
    pImageDescriptor->layoutBinding.descriptorType  = descriptorType;
    pImageDescriptor->layoutBinding.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS;
    pImageDescriptor->layoutBinding.binding         = binding;
    pImageDescriptor->layoutBinding.descriptorCount = static_cast<uint32_t>(pImageDescriptor->imageInfo.size());

    pImageDescriptor->imageInfo[arrayElement].imageView   = imageView;
    pImageDescriptor->imageInfo[arrayElement].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    pImageDescriptor->writeDescriptorSet                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    pImageDescriptor->writeDescriptorSet.dstSet          = VK_NULL_HANDLE;
    pImageDescriptor->writeDescriptorSet.descriptorType  = descriptorType;
    pImageDescriptor->writeDescriptorSet.dstBinding      = binding;
    pImageDescriptor->writeDescriptorSet.pImageInfo      = DataPtr(pImageDescriptor->imageInfo);
    pImageDescriptor->writeDescriptorSet.descriptorCount = CountU32(pImageDescriptor->imageInfo);
}

void WriteDescriptor(
    VulkanRenderer*       pRenderer,
    void*                 pDescriptorBufferStartAddress,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t              binding,
    uint32_t              arrayElement,
    VkDescriptorType      descriptorType,
    VkImageView           imageView,
    VkImageLayout         imageLayout)
{
    // Get the descriptor buffer properties so we can look up the descriptor size
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2                   properties                 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext                                                         = &descriptorBufferProperties;
    vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

    // Address info
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView             = imageView;
    imageInfo.imageLayout           = imageLayout;

    // Get buffer device address for acceleration structure
    VkDescriptorGetInfoEXT descriptorInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    descriptorInfo.type                   = descriptorType;

    // Set address info and Figure out descriptor size
    VkDeviceSize descriptorSize = 0;
    switch (descriptorType)
    {
        default: break;

        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        {
            descriptorInfo.data.pSampledImage = &imageInfo;
            descriptorSize                    = static_cast<VkDeviceSize>(descriptorBufferProperties.sampledImageDescriptorSize);
        }
        break;

        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        {
            descriptorInfo.data.pSampledImage = &imageInfo;
            descriptorSize                    = static_cast<VkDeviceSize>(descriptorBufferProperties.storageImageDescriptorSize);
        }
        break;
    }

    // Get the offset for the binding
    VkDeviceSize bindingOffset = 0;
    fn_vkGetDescriptorSetLayoutBindingOffsetEXT(
        pRenderer->Device,
        descriptorSetLayout,
        binding,
        &bindingOffset);

    // Calculate array element offset
    VkDeviceSize arrayElementOffset = arrayElement * descriptorSize;

    // Descriptor location
    VkDeviceSize location = bindingOffset + arrayElementOffset;

    // Write the descriptor
    char* pDescriptor = static_cast<char*>(pDescriptorBufferStartAddress) + location;
    fn_vkGetDescriptorEXT(
        pRenderer->Device, // device
        &descriptorInfo,   // pDescriptorInfo
        descriptorSize,    // dataSize
        pDescriptor);      // pDescriptor
}

void CreateDescriptor(
    VulkanRenderer*        pRenderer,
    VulkanImageDescriptor* pImageDescriptor,
    uint32_t               binding,
    uint32_t               arrayElement,
    VkSampler              sampler)
{
    assert(pImageDescriptor);

    pImageDescriptor->layoutBinding                 = {};
    pImageDescriptor->layoutBinding.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    pImageDescriptor->layoutBinding.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS;
    pImageDescriptor->layoutBinding.binding         = binding;
    pImageDescriptor->layoutBinding.descriptorCount = static_cast<uint32_t>(pImageDescriptor->imageInfo.size());

    pImageDescriptor->imageInfo[arrayElement].sampler = sampler;

    pImageDescriptor->writeDescriptorSet                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    pImageDescriptor->writeDescriptorSet.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    pImageDescriptor->writeDescriptorSet.dstBinding      = binding;
    pImageDescriptor->writeDescriptorSet.pImageInfo      = DataPtr(pImageDescriptor->imageInfo);
    pImageDescriptor->writeDescriptorSet.descriptorCount = CountU32(pImageDescriptor->imageInfo);
}

void WriteDescriptor(
    VulkanRenderer*       pRenderer,
    void*                 pDescriptorBufferStartAddress,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t              binding,
    uint32_t              arrayElement,
    VkSampler             sampler)
{
    // Get the descriptor buffer properties so we can look up the descriptor size
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2                   properties                 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext                                                         = &descriptorBufferProperties;
    vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

    // Get buffer device address for acceleration structure
    VkDescriptorGetInfoEXT descriptorInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    descriptorInfo.type                   = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorInfo.data.pSampler          = &sampler;

    // Descriptor size
    VkDeviceSize descriptorSize = static_cast<uint32_t>(descriptorBufferProperties.samplerDescriptorSize);

    // Get the offset for the binding
    VkDeviceSize bindingOffset = 0;
    fn_vkGetDescriptorSetLayoutBindingOffsetEXT(
        pRenderer->Device,
        descriptorSetLayout,
        binding,
        &bindingOffset);

    // Calculate array element offset
    VkDeviceSize arrayElementOffset = arrayElement * descriptorSize;

    // Descriptor location
    VkDeviceSize location = bindingOffset + arrayElementOffset;

    // Write the descriptor
    char* pDescriptor = static_cast<char*>(pDescriptorBufferStartAddress) + location;
    fn_vkGetDescriptorEXT(
        pRenderer->Device, // device
        &descriptorInfo,   // pDescriptorInfo
        descriptorSize,    // dataSize
        pDescriptor);      // pDescriptor
}

void PushGraphicsDescriptor(
    VkCommandBuffer     commandBuffer,
    VkPipelineLayout    pipelineLayout,
    uint32_t            set,
    uint32_t            binding,
    VkDescriptorType    descriptorType,
    const VulkanBuffer* pBuffer)
{
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer                 = pBuffer->Buffer;
    bufferInfo.offset                 = 0;
    bufferInfo.range                  = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet               = VK_NULL_HANDLE;
    write.dstBinding           = binding;
    write.dstArrayElement      = 0;
    write.descriptorCount      = 1;
    write.descriptorType       = descriptorType;
    write.pBufferInfo          = &bufferInfo;

    fn_vkCmdPushDescriptorSetKHR(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        set,
        1,
        &write);
}

void CreateAndUpdateDescriptorSet(
    VulkanRenderer*                            pRenderer,
    std::vector<VkDescriptorSetLayoutBinding>& layoutBindings,
    std::vector<VkWriteDescriptorSet>&         writeDescriptorSets,
    VulkanDescriptorSet*                       pDescriptors)
{
    // Allocate the Descriptor Pool
    std::map<VkDescriptorType, size_t> poolTypeCounts;
    for (auto& writeDescriptor : writeDescriptorSets)
    {
        poolTypeCounts[writeDescriptor.descriptorType] += writeDescriptor.descriptorCount;
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.resize(poolTypeCounts.size());

    size_t sizeIndex = 0;
    for (auto& typeCount : poolTypeCounts)
    {
        poolSizes[sizeIndex].type            = typeCount.first;
        poolSizes[sizeIndex].descriptorCount = static_cast<uint32_t>(typeCount.second);

        sizeIndex++;
    }

    VkDescriptorPoolCreateInfo poolCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCreateInfo.maxSets                    = 1;
    poolCreateInfo.poolSizeCount              = CountU32(poolSizes);
    poolCreateInfo.pPoolSizes                 = DataPtr(poolSizes);

    VkResult vkRes = vkCreateDescriptorPool(pRenderer->Device, &poolCreateInfo, nullptr, &pDescriptors->DescriptorPool);
    assert(vkRes == VK_SUCCESS);

    // Setup the Descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCreateInfo.pBindings                       = DataPtr(layoutBindings);
    layoutCreateInfo.bindingCount                    = CountU32(layoutBindings);

    vkRes = vkCreateDescriptorSetLayout(pRenderer->Device, &layoutCreateInfo, nullptr, &pDescriptors->DescriptorSetLayout);
    assert(vkRes == VK_SUCCESS);

    // Setup the descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool              = pDescriptors->DescriptorPool;
    allocInfo.pSetLayouts                 = &pDescriptors->DescriptorSetLayout;
    allocInfo.descriptorSetCount          = 1;

    vkRes = vkAllocateDescriptorSets(pRenderer->Device, &allocInfo, &pDescriptors->DescriptorSet);
    assert(vkRes == VK_SUCCESS);

    // Copy the newly create descriptor set into all the already created write descriptor sets
    for (auto& writeDescriptor : writeDescriptorSets)
    {
        writeDescriptor.dstSet = pDescriptors->DescriptorSet;
    }

    // Update all descriptor sets
    vkUpdateDescriptorSets(pRenderer->Device, CountU32(writeDescriptorSets), DataPtr(writeDescriptorSets), 0, nullptr);
}

uint32_t BytesPerPixel(VkFormat fmt)
{
    uint32_t nbytes = BitsPerPixel(fmt) / 8;
    return nbytes;
}

bool IsCompressed(VkFormat fmt)
{
    switch (fmt)
    {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;

        default:
            return false;
    }
}

uint32_t BitsPerPixel(VkFormat fmt)
{
    switch (fmt)
    {
        case VK_FORMAT_R4G4_UNORM_PACK8:
            return 8;

        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
        case VK_FORMAT_B5G6R5_UNORM_PACK16:
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
            return 16;

        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_USCALED:
        case VK_FORMAT_R8_SSCALED:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8_SRGB:
            return 8;

        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_USCALED:
        case VK_FORMAT_R8G8_SSCALED:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8_SRGB:
            return 16;

        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_USCALED:
        case VK_FORMAT_R8G8B8_SSCALED:
        case VK_FORMAT_R8G8B8_UINT:
        case VK_FORMAT_R8G8B8_SINT:
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_UNORM:
        case VK_FORMAT_B8G8R8_SNORM:
        case VK_FORMAT_B8G8R8_USCALED:
        case VK_FORMAT_B8G8R8_SSCALED:
        case VK_FORMAT_B8G8R8_UINT:
        case VK_FORMAT_B8G8R8_SINT:
        case VK_FORMAT_B8G8R8_SRGB:
            return 24;

        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_USCALED:
        case VK_FORMAT_R8G8B8A8_SSCALED:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_USCALED:
        case VK_FORMAT_B8G8R8A8_SSCALED:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_B8G8R8A8_SINT:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:
        case VK_FORMAT_A8B8G8R8_SINT_PACK32:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
        case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:
        case VK_FORMAT_A2R10G10B10_SINT_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
        case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
        case VK_FORMAT_A2B10G10R10_SINT_PACK32:
            return 32;

        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_USCALED:
        case VK_FORMAT_R16_SSCALED:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16_SFLOAT:
            return 16;

        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_USCALED:
        case VK_FORMAT_R16G16_SSCALED:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16_SFLOAT:
            return 32;

        case VK_FORMAT_R16G16B16_UNORM:
        case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_USCALED:
        case VK_FORMAT_R16G16B16_SSCALED:
        case VK_FORMAT_R16G16B16_UINT:
        case VK_FORMAT_R16G16B16_SINT:
        case VK_FORMAT_R16G16B16_SFLOAT:
            return 48;

        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_USCALED:
        case VK_FORMAT_R16G16B16A16_SSCALED:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 64;

        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32_SFLOAT:
            return 32;

        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32_SFLOAT:
            return 64;

        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
        case VK_FORMAT_R32G32B32_SFLOAT:
            return 96;

        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 128;

        case VK_FORMAT_R64_UINT:
        case VK_FORMAT_R64_SINT:
        case VK_FORMAT_R64_SFLOAT:
            return 64;

        case VK_FORMAT_R64G64_UINT:
        case VK_FORMAT_R64G64_SINT:
        case VK_FORMAT_R64G64_SFLOAT:
            return 128;

        case VK_FORMAT_R64G64B64_UINT:
        case VK_FORMAT_R64G64B64_SINT:
        case VK_FORMAT_R64G64B64_SFLOAT:
            return 192;

        case VK_FORMAT_R64G64B64A64_UINT:
        case VK_FORMAT_R64G64B64A64_SINT:
        case VK_FORMAT_R64G64B64A64_SFLOAT:
            return 256;

        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
            return 32;

        case VK_FORMAT_D16_UNORM:
            return 16;

        // Implementation dependant
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return 0;

        case VK_FORMAT_D32_SFLOAT:
            return 32;

        case VK_FORMAT_S8_UINT:
            return 8;

        case VK_FORMAT_D16_UNORM_S8_UINT:
            return 24;

        case VK_FORMAT_D24_UNORM_S8_UINT:
            return 32;

        // Implementation dependant
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return 0;

        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return 64;

        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return 128;

        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 64;

        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return 128;

        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
            return 64;

        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
            return 128;

        case VK_FORMAT_EAC_R11_UNORM_BLOCK:
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:
            return 64;

        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
            return 128;

        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
            return 128;

        case VK_FORMAT_G8B8G8R8_422_UNORM:
        case VK_FORMAT_B8G8R8G8_422_UNORM:
            return 32;

        // TODO: Figure these out
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_R10X6_UNORM_PACK16:
        case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
        case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
        case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_R12X4_UNORM_PACK16:
        case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
        case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
        case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
            return 0;

        case VK_FORMAT_G16B16G16R16_422_UNORM:
        case VK_FORMAT_B16G16R16G16_422_UNORM:
            return 64;

        // TODO: Figure these out
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:
            return 0;

        case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
        case VK_FORMAT_A4B4G4R4_UNORM_PACK16:
            return 16;

        case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK:
            return 128;

        case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
            return 64;

        case VK_FORMAT_R16G16_S10_5_NV:
            return 0;

        default:
            return 0;
    }
}
