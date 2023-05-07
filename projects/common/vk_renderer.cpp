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

uint32_t BitsPerPixel(VkFormat fmt);

uint32_t PixelStride(VkFormat fmt)
{
    uint32_t nbytes = BitsPerPixel(fmt) / 8;
    return nbytes;
}

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

// =================================================================================================
// DxRenderer
// =================================================================================================
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
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};

        if (pRenderer->RayTracingEnabled) {
            enabledExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
            enabledExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            enabledExtensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
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
    VkDeviceSize        minAlignment,
    VulkanBuffer*       pBuffer)
{
    VkResult vkres = CreateBuffer(
        pRenderer,
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        minAlignment,
        pBuffer);
    if (vkres != VK_SUCCESS) {
        return vkres;
    }

    return VK_SUCCESS;
}

VkResult CreateTexture(
    VulkanRenderer*                 pRenderer,
    uint32_t                        width,
    uint32_t                        height,
    VkFormat                        format,
    const std::vector<VkMipOffset>& mipOffsets,
    uint64_t                        srcSizeBytes,
    const void*                     pSrcData,
    VulkanImage*                    pImage)
{
    if (IsNull(pRenderer)) {
        return VK_ERROR_UNKNOWN;
    }
    if (IsNull(ppResource)) {
        return VK_ERROR_UNKNOWN;
    }
    if ((format == VK_FORMAT_UNDEFINED) || IsVideo(format)) {
        return VK_ERROR_UNKNOWN;
    }
    if (mipOffsets.empty()) {
        return VK_ERROR_UNKNOWN;
    }

    UINT              mipLevels = static_cast<UINT>(mipOffsets.size());
    VkImageCreateInfo vkci      = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    vkci.imageType              = VK_IMAGE_TYPE_2D;
    vkci.format                 = format;
    vkci.extent.width           = width;
    vkci.extent.height          = height;
    vkci.extent.depth           = depth;
    vkci.mipLevels              = mipLevels;
    vkci.arrayLayers            = 1;
    vkci.usage                  = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vkci.initialLayout          = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    vkci.samples                = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage                   = memoryUsage;

    VkResult vkres = vmaCreateImage(
        pRenderer->Allocator,
        &vkci,
        &allocCreateInfo,
        &pImage->Image,
        &pImage->Allocation,
        &pImage->AllocationInfo);

    if (vkres != VK_SUCCESS) {
        return vkres;
    }

    if (!IsNull(pSrcData)) {
        const uint32_t rowStride = width * PixelStride(format);
        // Calculate the total number of rows for all mip maps
        uint32_t numRows = 0;
        {
            uint32_t mipHeight = height;
            for (UINT level = 0; level < mipLevels; ++level) {
                numRows += mipHeight;
                mipHeight >>= 1;
            }
        }

        VulkanBuffer stagingBuffer;
        vkres = CreateBuffer(pRenderer, rowStride * numRows, pSrcData, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stagingBuffer);
        if (vkres != VK_SUCCESS) {
            assert(false && "create staging buffer failed");
            return vkres;
        }

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

        // Build command buffer
        {
            uint32_t levelWidth  = width;
            uint32_t levelHeight = height;
            for (UINT level = 0; level < mipLevels; ++level) {
                const auto&    mipOffset    = mipOffsets[level];
                const uint32_t mipRowStride = mipOffset.rowStride;

                VkImageAspectFlagBits aspectFlags     = VK_IMAGE_ASPECT_COLOR_BIT;
                VkBufferImageCopy     srcRegion       = {};
                srcRegion.bufferOffset                = mipOffset.offset;
                srcRegion.bufferRowLength             = mipRowStride;
                srcRegion.bufferImageHeight           = height;
                srcRegion.imageSubresource.aspectMask = aspectFlags;
                srcRegion.imageSubresource.layerCount = 1 srcRegion.imageSubresource.mipLevel = level;
                srcRegion.imageExtent.width                                                   = width;
                srcRegion.imageExtent.height                                                  = height;
                srcRegion.imageExtent.depth                                                   = 1;

                vkCmdCopyBufferToImage(cmdBuf.CommandBuffer, stagingBuffer->Buffer, pImage->Image, format, 1, &srcRegion);

                VkImageMemoryBarrier2 barrier           = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.pNext                           = nullptr;
                barrier.srcStageMask                    = 0;
                barrier.srcAccessMask                   = 0;
                barrier.dstStageMask                    = 0;
                barrier.dstAccessMask                   = 0;
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                           = pImage->Image;
                barrier.subresourceRange.aspectMask     = aspectFlags;
                barrier.subresourceRange.baseMipLevel   = level;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;

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

                levelWidth >>= 1;
                levelHeight >>= 1;
            }
        }

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
    }

    return VK_SUCCESS;
}

VkResult CreateTexture(
    VulkanRenderer* pRenderer,
    uint32_t        width,
    uint32_t        height,
    uint64_t        srcSizeBytes,
    const void*     pSrcData,
    VulkanImage*    pImage)
{
    VkMipOffset mipOffset = {};
    mipOffset.offset      = 0;
    mipOffset.rowStride   = width * PixelStride(format);

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

VkResult Create2DImage(
    VulkanRenderer*   pRenderer,
    VkImageType       imageType,
    VkImageUsageFlags imageUsage,
    uint32_t          width,
    uint32_t          height,
    uint32_t          depth,
    VkFormat          format,
    VkImageLayout     initialLayout,
    VmaMemoryUsage    memoryUsage,
    VulkanImage*      pImage)
{
    if (IsNull(pImage)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageCreateInfo vkci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    vkci.imageType         = imageType;
    vkci.format            = format;
    vkci.extent.width      = width;
    vkci.extent.height     = height;
    vkci.extent.depth      = depth;
    vkci.mipLevels         = 1;
    vkci.arrayLayers       = 1;
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

VkResult Create2DImage(
    VulkanRenderer*   pRenderer,
    VkImageType       imageType,
    VkImageUsageFlags imageUsage,
    uint32_t          width,
    uint32_t          height,
    uint32_t          depth,
    VkMipOffset       mipOffset,
    VkFormat          format,
    VkImageLayout     initialLayout,
    VmaMemoryUsage    memoryUsage,
    VulkanImage*      pImage)
{
    if (IsNull(pImage)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageCreateInfo vkci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    vkci.imageType         = imageType;
    vkci.format            = format;
    vkci.extent.width      = width;
    vkci.extent.height     = height;
    vkci.extent.depth      = depth;
    vkci.mipLevels         = 1;
    vkci.arrayLayers       = 1;
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

VkResult CreateDSV(
    VulkanRenderer* pRenderer,
    uint32_t        width,
    uint32_t        height,
    VulkanImage*    pImage)
{
    return Create2DImage(
        pRenderer,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        width,
        height,
        1,
        GREX_DEFAULT_DSV_FORMAT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VMA_MEMORY_USAGE_GPU_ONLY,
        pImage);
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

VkResult CreateDrawVertexColorPipeline(
    VulkanRenderer*  pRenderer,
    VkPipelineLayout pipeline_layout,
    VkShaderModule   vsShaderModule,
    VkShaderModule   fsShaderModule,
    VkFormat         rtvFormat,
    VkFormat         dsvFormat,
    VkPipeline*      pPipeline,
    VkCullModeFlags  cullMode)
{
    VkFormat                      rtv_format                     = GREX_DEFAULT_RTV_FORMAT;
    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipeline_rendering_create_info.colorAttachmentCount          = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats       = &rtv_format;
    pipeline_rendering_create_info.depthAttachmentFormat         = GREX_DEFAULT_DSV_FORMAT;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vsShaderModule;
    shader_stages[0].pName                           = "main";
    shader_stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module                          = fsShaderModule;
    shader_stages[1].pName                           = "main";

    VkVertexInputBindingDescription vertex_binding_desc[2] = {};
    vertex_binding_desc[0].binding                         = 0;
    vertex_binding_desc[0].stride                          = 12;
    vertex_binding_desc[0].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

    vertex_binding_desc[1].binding   = 1;
    vertex_binding_desc[1].stride    = 12;
    vertex_binding_desc[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute_desc[2] = {};
    vertex_attribute_desc[0].location                          = 0;
    vertex_attribute_desc[0].binding                           = 0;
    vertex_attribute_desc[0].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_desc[0].offset                            = 0;

    vertex_attribute_desc[1].location = 1;
    vertex_attribute_desc[1].binding  = 1;
    vertex_attribute_desc[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
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

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext                        = &pipeline_rendering_create_info;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_state;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterization_state;
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


static std::wstring AsciiToUTF16(const std::string& ascii)
{
    std::wstring utf16;
    for (auto& c : ascii) {
        utf16.push_back(static_cast<std::wstring::value_type>(c));
    }
    return utf16;
}

HRESULT CompileHLSL(
    const std::string& shaderSource,
    const std::string& entryPoint,
    const std::string& profile,
    std::vector<char>* pSpirv,
    std::string*       pErrorMsg)
{
    // Check source
    if (shaderSource.empty()) {
        assert(false && "no shader source");
        return E_INVALIDARG;
    }
    // Check entry point
    if (entryPoint.empty() && (!profile.starts_with("lib_6_"))) {
        assert(false && "no entrypoint");
        return E_INVALIDARG;
    }
    // Check profile
    if (profile.empty()) {
        assert(false && "no profile");
        return E_INVALIDARG;
    }
    // Check output
    if (IsNull(pSpirv)) {
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

    std::vector<LPCWSTR> args = {L"-spirv"};
   
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
    if (FAILED(hr)) {
        assert(false && "compile failed");
        return hr;
    }

    ComPtr<IDxcBlob> errors;
    hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (FAILED(hr)) {
        assert(false && "Get error output failed");
        return hr;
    }
    if (errors && (errors->GetBufferSize() > 0) && !IsNull(pErrorMsg)) {
        const char* pBuffer    = static_cast<const char*>(errors->GetBufferPointer());
        size_t      bufferSize = static_cast<size_t>(errors->GetBufferSize());
        *pErrorMsg             = std::string(pBuffer, pBuffer + bufferSize);
        return E_FAIL;
    }

    ComPtr<IDxcBlob> shaderBinary;
    hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBinary), nullptr);
    if (FAILED(hr)) {
        assert(false && "Get compile output failed");
        return hr;
    }

    const char* pBuffer    = static_cast<const char*>(shaderBinary->GetBufferPointer());
    size_t      bufferSize = static_cast<size_t>(shaderBinary->GetBufferSize());
    *pSpirv                 = std::vector<char>(pBuffer, pBuffer + bufferSize);

    return S_OK;
}

uint32_t BitsPerPixel(VkFormat fmt)
{
    switch (fmt) {
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