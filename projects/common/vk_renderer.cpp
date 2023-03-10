#include "vk_renderer.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "glslang/Include/glslang_c_interface.h"
#include "glslang/Public/resource_limits_c.h"

#define VK_KHR_VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"

#define VK_QUEUE_MASK_ALL_TYPES (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
#define VK_QUEUE_MASK_GRAPHICS  (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
#define VK_QUEUE_MASK_COMPUTE   (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
#define VK_QUEUE_MASK_TRANSFER  (VK_QUEUE_TRANSFER_BIT)

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

std::vector<std::string> EnumeratePhysicalDeviceExtensionNames(VkPhysicalDevice physicalDevice)
{
    uint32_t count = 0;
    VkResult vkres = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    if (vkres != VK_SUCCESS) {
        return {};
    }
    std::vector<VkExtensionProperties> propertiesList(count);
    vkres = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, DataPtr(propertiesList));
    if (vkres != VK_SUCCESS) {
        return {};
    }
    std::vector<std::string> names;
    for (auto& properties : propertiesList) {
        names.push_back(properties.extensionName);
    }
    return names;
}

VulkanRenderer::VulkanRenderer()
{
}

VulkanRenderer::~VulkanRenderer()
{
}

CommandObjects::~CommandObjects()
{
    if (CommandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(this->pRenderer->Device, this->CommandPool, 1, &CommandBuffer);
    }

    if (CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(this->pRenderer->Device, this->CommandPool, nullptr);
    }
}

bool InitVulkan(VulkanRenderer* pRenderer, bool enableDebug, bool enableRayTracing, uint32_t apiVersion)
{
    if (IsNull(pRenderer)) {
        return false;
    }

    pRenderer->DebugEnabled      = enableDebug;
    pRenderer->RayTracingEnabled = enableRayTracing;

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
        if (pRenderer->DebugEnabled) {
            enabledLayers.push_back(VK_KHR_VALIDATION_LAYER_NAME);
        }

        std::vector<const char*> enabledExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
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
        if (vkres != VK_SUCCESS) {
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
    }

    // Physical device
    {
        uint32_t count = 0;
        VkResult vkres = vkEnumeratePhysicalDevices(pRenderer->Instance, &count, nullptr);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkEnumeratePhysicalDevices failed");
            return false;
        }

        std::vector<VkPhysicalDevice> enumeratedPhysicalDevices(count);
        vkres = vkEnumeratePhysicalDevices(pRenderer->Instance, &count, DataPtr(enumeratedPhysicalDevices));
        if (vkres != VK_SUCCESS) {
            assert(false && "vkEnumeratePhysicalDevices failed");
            return false;
        }

        std::vector<VkPhysicalDevice> physicalDevices;
        for (auto& physicalDevice : enumeratedPhysicalDevices) {
            VkPhysicalDeviceProperties properties = {};
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);
            if ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
                (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)) {
                physicalDevices.push_back(physicalDevice);
            }
        }

        if (physicalDevices.empty()) {
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

        for (uint32_t i = 0; i < count; ++i) {
            auto& properties = propertiesList[i];
            if ((properties.queueFlags & VK_QUEUE_MASK_ALL_TYPES) == VK_QUEUE_MASK_GRAPHICS) {
                pRenderer->GraphicsQueueFamilyIndex = i;
                break;
            }
        }

        if (pRenderer->GraphicsQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED) {
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
            VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        if (pRenderer->RayTracingEnabled) {
            enabledExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        }

        // Make sure all the extenions are present
        auto enumeratedExtensions = EnumeratePhysicalDeviceExtensionNames(pRenderer->PhysicalDevice);
        for (auto& elem : enabledExtensions) {
            if (!Contains(std::string(elem), enumeratedExtensions)) {
                GREX_LOG_ERROR("extension not found: " << elem);
                assert(false);
                return false;
            }
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

        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
        bufferDeviceAddressFeatures.pNext                                       = pRenderer->RayTracingEnabled ? &rayTracingPipelineFeatures : nullptr;
        bufferDeviceAddressFeatures.bufferDeviceAddress                         = VK_TRUE;

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures         = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &bufferDeviceAddressFeatures};
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

        VkPhysicalDeviceSynchronization2Features synchronization2Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        synchronization2Features.pNext                                    = &dynamicRenderingFeatures;
        synchronization2Features.synchronization2                         = VK_TRUE;

        VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, &synchronization2Features};
        timelineSemaphoreFeatures.timelineSemaphore                         = VK_TRUE;

        VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT, &timelineSemaphoreFeatures};
        descriptorBufferFeatures.descriptorBuffer                            = VK_TRUE;

        VkPhysicalDeviceFeatures enabledFeatures = {};

        VkDeviceCreateInfo vkci      = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        vkci.pNext                   = &descriptorBufferFeatures;
        vkci.flags                   = 0;
        vkci.queueCreateInfoCount    = 1;
        vkci.pQueueCreateInfos       = &queueCreateInfo;
        vkci.enabledLayerCount       = 0;
        vkci.ppEnabledLayerNames     = nullptr;
        vkci.enabledExtensionCount   = CountU32(enabledExtensions);
        vkci.ppEnabledExtensionNames = DataPtr(enabledExtensions);
        vkci.pEnabledFeatures        = &enabledFeatures;

        VkResult vkres = vkCreateDevice(pRenderer->PhysicalDevice, &vkci, nullptr, &pRenderer->Device);
        if (vkres != VK_SUCCESS) {
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
        if (vkres != VK_SUCCESS) {
            assert(false && "vmaCreateAllocator failed");
            return false;
        }
    }

    return true;
}

bool InitSwapchain(VulkanRenderer* pRenderer, HWND hwnd, uint32_t width, uint32_t height, uint32_t imageCount)
{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    {
        VkWin32SurfaceCreateInfoKHR vkci = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        //
        vkci.pNext     = nullptr;
        vkci.flags     = 0;
        vkci.hinstance = ::GetModuleHandle(nullptr);
        vkci.hwnd      = hwnd;

        VkResult vkres = vkCreateWin32SurfaceKHR(pRenderer->Instance, &vkci, nullptr, &pRenderer->Surface);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkCreateWin32SurfaceKHR failed");
            return false;
        }
    }
#endif

    // Surface caps
    VkSurfaceCapabilitiesKHR surfaceCaps = {};
    {
        VkResult vkres = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pRenderer->PhysicalDevice, pRenderer->Surface, &surfaceCaps);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
            return false;
        }
    }

    // Swapchain
    {
        imageCount = std::max<uint32_t>(imageCount, surfaceCaps.minImageCount);
        if (surfaceCaps.maxImageCount > 0) {
            imageCount = std::min<uint32_t>(imageCount, surfaceCaps.maxImageCount);
        }

        VkSwapchainCreateInfoKHR vkci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        //
        vkci.pNext                 = nullptr;
        vkci.flags                 = 0;
        vkci.surface               = pRenderer->Surface;
        vkci.minImageCount         = imageCount;
        vkci.imageFormat           = VK_FORMAT_R8G8B8A8_UNORM;
        vkci.imageColorSpace       = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        vkci.imageExtent           = {width, height};
        vkci.imageArrayLayers      = 1;
        vkci.imageUsage            = surfaceCaps.supportedUsageFlags;
        vkci.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        vkci.queueFamilyIndexCount = 0;
        vkci.pQueueFamilyIndices   = nullptr;
        vkci.preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        vkci.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        vkci.presentMode           = VK_PRESENT_MODE_IMMEDIATE_KHR;
        vkci.clipped               = VK_FALSE;
        vkci.oldSwapchain          = VK_NULL_HANDLE;

        VkResult vkres = vkCreateSwapchainKHR(pRenderer->Device, &vkci, nullptr, &pRenderer->Swapchain);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkCreateSwapchainKHR failed");
            return false;
        }

        pRenderer->SwapchainImageCount = imageCount;
    }

    // Transition image layouts to present
    {
        std::vector<VkImage> images;
        VkResult             vkres = GetSwapchainImages(pRenderer, images);
        if (vkres != VK_SUCCESS) {
            assert(false && "GetSwapchainImages failed");
            return false;
        }

        for (auto& image : images) {
            vkres = TransitionImageLayout(pRenderer, image, GREX_ALL_SUBRESOURCES, VK_IMAGE_ASPECT_COLOR_BIT, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_PRESENT);
            if (vkres != VK_SUCCESS) {
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
        if (vkres != VK_SUCCESS) {
            assert(false && "vkCreateSemaphore failed");
            return false;
        }

        vkres = vkCreateSemaphore(pRenderer->Device, &vkci, nullptr, &pRenderer->PresentReadySemaphore);
        if (vkres != VK_SUCCESS) {
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
        if (vkres != VK_SUCCESS) {
            assert(false && "vkCreateFence failed");
            return false;
        }
    }

    return true;
}

bool WaitForGpu(VulkanRenderer* pRenderer)
{
    VkResult vkres = vkQueueWaitIdle(pRenderer->Queue);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkQueueWaitIdle failed");
        return false;
    }

    return true;
}

VkResult GetSwapchainImages(VulkanRenderer* pRenderer, std::vector<VkImage>& images)
{
    uint32_t count = 0;
    VkResult vkres = vkGetSwapchainImagesKHR(pRenderer->Device, pRenderer->Swapchain, &count, nullptr);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkGetSwapchainImagesKHR failed");
        return vkres;
    }
    images.resize(count);
    vkres = vkGetSwapchainImagesKHR(pRenderer->Device, pRenderer->Swapchain, &count, DataPtr(images));
    if (vkres != VK_SUCCESS) {
        assert(false && "vkGetSwapchainImagesKHR failed");
        return vkres;
    }
    return VK_SUCCESS;
}

VkResult AcquireNextImage(VulkanRenderer* pRenderer, uint32_t* pImageIndex)
{
    VkResult vkres = vkAcquireNextImageKHR(pRenderer->Device, pRenderer->Swapchain, UINT64_MAX, VK_NULL_HANDLE, pRenderer->ImageReadyFence, pImageIndex);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkAcquireNextImageKHR failed");
        return vkres;
    }

    vkres = vkWaitForFences(pRenderer->Device, 1, &pRenderer->ImageReadyFence, VK_TRUE, UINT64_MAX);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkWaitForFences failed");
        return vkres;
    }

    vkres = vkResetFences(pRenderer->Device, 1, &pRenderer->ImageReadyFence);
    if (vkres != VK_SUCCESS) {
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
    if (vkres != VK_SUCCESS) {
        assert(false && "vkQueuePresentKHR failed");
        return false;
    }

    return true;
}

VkResult CreateCommandBuffer(VulkanRenderer* pRenderer, VkCommandPoolCreateFlags poolCreateFlags, CommandObjects* pCmdBuf)
{
    if (IsNull(pCmdBuf)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *pCmdBuf = {pRenderer};

    VkCommandPoolCreateInfo vkci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    vkci.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | poolCreateFlags;
    vkci.queueFamilyIndex        = pRenderer->GraphicsQueueFamilyIndex;

    VkResult vkres = vkCreateCommandPool(pRenderer->Device, &vkci, nullptr, &pCmdBuf->CommandPool);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkCreateCommandPool failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBufferAllocateInfo vkai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    vkai.commandPool                 = pCmdBuf->CommandPool;
    vkai.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkai.commandBufferCount          = 1;

    vkres = vkAllocateCommandBuffers(pRenderer->Device, &vkai, &pCmdBuf->CommandBuffer);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkAllocateCommandBuffers failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return VK_SUCCESS;
}

VkResult ExecuteCommandBuffer(VulkanRenderer* pRenderer, const CommandObjects* pCmdBuf)
{
    if (IsNull(pCmdBuf)) {
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
        VK_NULL_HANDLE);
    if (vkres != VK_SUCCESS) {
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

    switch (state) {
        default: {
            return false;
        } break;

        case RESOURCE_STATE_UNKNOWN: {
            stage_mask  = 0;
            access_mask = 0;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_COMMON: {
            stage_mask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            access_mask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_VERTEX_AND_UNIFORM_BUFFER: {
            stage_mask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            access_mask = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_INDEX_BUFFER: {
            stage_mask  = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            access_mask = VK_ACCESS_2_INDEX_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_RENDER_TARGET: {
            stage_mask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        } break;

        case RESOURCE_STATE_DEPTH_STENCIL: {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        } break;

        case RESOURCE_STATE_DEPTH_READ: {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        } break;

        case RESOURCE_STATE_STENCIL_READ: {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        } break;

        case RESOURCE_STATE_DEPTH_AND_STENCIL_READ: {
            stage_mask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        } break;

        case RESOURCE_STATE_VERTEX_SHADER_RESOURCE: {
            stage_mask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_HULL_SHADER_RESOURCE: {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_DOMAIN_SHADER_RESOURCE: {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_GEOMETRY_SHADER_RESOURCE: {
            stage_mask  = VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_PIXEL_SHADER_RESOURCE: {
            stage_mask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_COMPUTE_SHADER_RESOURCE: {
            stage_mask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_VERTEX_UNORDERED_ACCESS: {
            stage_mask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_HULL_UNORDERED_ACCESS: {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_DOMAIN_UNORDERED_ACCESS: {
            stage_mask  = VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_GEOMETRY_UNORDERED_ACCESS: {
            stage_mask  = VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_PIXEL_UNORDERED_ACCESS: {
            stage_mask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_COMPUTE_UNORDERED_ACCESS: {
            stage_mask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            access_mask = VK_ACCESS_2_SHADER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_GENERAL;
        } break;

        case RESOURCE_STATE_TRANSFER_DST: {
            stage_mask  = VK_PIPELINE_STAGE_2_COPY_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        } break;

        case RESOURCE_STATE_TRANSFER_SRC: {
            stage_mask  = VK_PIPELINE_STAGE_2_COPY_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        } break;

        case RESOURCE_STATE_RESOLVE_DST: {
            stage_mask  = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        } break;

        case RESOURCE_STATE_RESOLVE_SRC: {
            stage_mask  = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            access_mask = VK_ACCESS_2_TRANSFER_READ_BIT;
            layout      = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        } break;

        case RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE: {
            stage_mask  = 0;
            access_mask = 0;
            layout      = VK_IMAGE_LAYOUT_UNDEFINED;
        } break;

        case RESOURCE_STATE_PRESENT: {
            stage_mask  = 0;
            access_mask = 0;
            layout      = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        } break;
    }

    if (!IsNull(pStageMask)) {
        *pStageMask = stage_mask;
    }

    if (!IsNull(pAccessMask)) {
        *pAccessMask = access_mask;
    }

    if (!IsNull(pLayout)) {
        *pLayout = layout;
    }

    return true;
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
    if (vkres != VK_SUCCESS) {
        assert(false && "CreateCommandBuffer failed");
        return vkres;
    }

    VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkres = vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkBeginCommandBuffer failed");
        return vkres;
    }

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

    vkCmdPipelineBarrier2(cmdBuf.CommandBuffer, &dependencyInfo);

    vkres = vkEndCommandBuffer(cmdBuf.CommandBuffer);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkEndCommandBuffer failed");
        return vkres;
    }

    vkres = ExecuteCommandBuffer(pRenderer, &cmdBuf);
    if (vkres != VK_SUCCESS) {
        assert(false && "ExecuteCommandBuffer failed");
        return vkres;
    }

    vkres = vkQueueWaitIdle(pRenderer->Queue);
    if (vkres != VK_SUCCESS) {
        assert(false && "vkQueueWaitIdle failed");
        return vkres;
    }

    return VK_SUCCESS;
}

VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             srcSize,
    VkBufferUsageFlags usageFlags,
    VmaMemoryUsage     memoryUsage,
    VkDeviceSize       minAlignment,
    VulkanBuffer*      pBuffer)
{
    if (IsNull(pBuffer)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferCreateInfo vkci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vkci.pNext              = nullptr;
    vkci.size               = srcSize;
    vkci.usage              = usageFlags;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage                   = memoryUsage;

    if (minAlignment > 0) {
        VkResult vkres = vmaCreateBufferWithAlignment(
            pRenderer->Allocator,
            &vkci,
            &allocCreateInfo,
            minAlignment,
            &pBuffer->Buffer,
            &pBuffer->Allocation,
            &pBuffer->AllocationInfo);
        if (vkres != VK_SUCCESS) {
            return vkres;
        }
    }
    else {
        VkResult vkres = vmaCreateBuffer(
            pRenderer->Allocator,
            &vkci,
            &allocCreateInfo,
            &pBuffer->Buffer,
            &pBuffer->Allocation,
            &pBuffer->AllocationInfo);
        if (vkres != VK_SUCCESS) {
            return vkres;
        }
    }

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
        VMA_MEMORY_USAGE_CPU_ONLY,
        minAlignment,
        pBuffer);
    if (vkres != VK_SUCCESS) {
        return vkres;
    }

    if (!IsNull(pSrcData)) {
        char*    pData = nullptr;
        VkResult vkres = vmaMapMemory(pRenderer->Allocator, pBuffer->Allocation, reinterpret_cast<void**>(&pData));
        if (vkres != VK_SUCCESS) {
            return vkres;
        }

        memcpy(pData, pSrcData, srcSize);

        vmaUnmapMemory(pRenderer->Allocator, pBuffer->Allocation);
    }

    return VK_SUCCESS;
}

VkResult CreateUAVBuffer(
    VulkanRenderer*     pRenderer,
    VkBufferCreateFlags createFlags,
    size_t              size,
    VkBufferUsageFlags  usageFlags,
    VkDeviceSize        minAlignment,
    VulkanBuffer*       pBuffer)
{
    return VK_SUCCESS;
}

void DestroyBuffer(VulkanRenderer* pRenderer, const VulkanBuffer* pBuffer)
{
    vmaDestroyBuffer(pRenderer->Allocator, pBuffer->Buffer, pBuffer->Allocation);
}

VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, const VulkanBuffer* pBuffer)
{
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

CompileResult CompileGLSL(
    const std::string&     shaderSource,
    const std::string&     entryPoint,
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
    if (glslang_stage == k_invalid_stage) {
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
    if (res == 0) {
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
            if (!IsNull(pObject)) {
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
            if (!IsNull(pObject)) {
                glslang_program_delete(pObject);
                pObject = nullptr;
            }
        }
    };

    ScopedShader shader = {};
    {
        glslang_shader_t* p_shader = glslang_shader_create(&input);

        if (IsNull(p_shader)) {
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
    if (!glslang_shader_preprocess(shader, &input)) {
        std::stringstream ss;

        const char* infoLog = glslang_shader_get_info_log(shader);
        if (infoLog != nullptr) {
            ss << "GLSL preprocess failed (info): " << infoLog;
        }

        const char* debugLog = glslang_shader_get_info_debug_log(shader);
        if (debugLog != nullptr) {
            ss << "GLSL preprocess failed (debug): " << debugLog;
        }

        if (!IsNull(pErrorMsg)) {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_PREPROCESS_FAILED;
    }

    //
    // Compile
    //
    if (!glslang_shader_parse(shader, &input)) {
        std::stringstream ss;

        const char* info_log = glslang_shader_get_info_log(shader);
        if (info_log != nullptr) {
            ss << "GLSL compile failed (info): " << info_log;
        }

        const char* debug_log = glslang_shader_get_info_debug_log(shader);
        if (debug_log != nullptr) {
            ss << "GLSL compile failed (debug): " << debug_log;
        }

        if (!IsNull(pErrorMsg)) {
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
        if (IsNull(p_program)) {
            return COMPILE_ERROR_INTERNAL_COMPILER_ERROR;
        }
        program.pObject = p_program;
    }
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        std::stringstream ss;

        const char* info_log = glslang_program_get_info_log(program);
        if (info_log != nullptr) {
            ss << "GLSL link failed (info): " << info_log;
        }

        const char* debug_log = glslang_program_get_info_debug_log(program);
        if (debug_log != nullptr) {
            ss << "GLSL link failed (debug): " << debug_log;
        }

        if (!IsNull(pErrorMsg)) {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_LINK_FAILED;
    }

    //
    // Map IO
    //
    if (!glslang_program_map_io(program)) {
        std::stringstream ss;

        ss << "GLSL program map IO failed";

        if (!IsNull(pErrorMsg)) {
            *pErrorMsg = ss.str();
        }

        return COMPILE_ERROR_MAP_IO_FAILED;
    }

    //
    // Get SPIR-V
    //
    if (!IsNull(pSPIRV)) {
        glslang_program_SPIRV_generate(program, input.stage);
        const char* spirv_msg = glslang_program_SPIRV_get_messages(program);
        if (!IsNull(spirv_msg)) {
            std::stringstream ss;
            ss << "SPIR-V generation error: " << spirv_msg;

            if (!IsNull(pErrorMsg)) {
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