#pragma once

#include "config.h"

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#define GREX_ALL_SUBRESOURCES 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS

// Use D3D12 style resource states to simplify synchronization
enum ResourceState
{
    RESOURCE_STATE_UNKNOWN                           = 0,  // No applicable in Direct3D, undefined for Vulkan
    RESOURCE_STATE_COMMON                            = 1,  // Common in Direct3D, General in Vulkan
    RESOURCE_STATE_VERTEX_AND_UNIFORM_BUFFER         = 2,  //
    RESOURCE_STATE_INDEX_BUFFER                      = 3,  //
    RESOURCE_STATE_RENDER_TARGET                     = 4,  //
    RESOURCE_STATE_DEPTH_STENCIL                     = 5,  // Depth and stecil write
    RESOURCE_STATE_DEPTH_READ                        = 6,  // Depth read , stencil write
    RESOURCE_STATE_STENCIL_READ                      = 7,  // Depth write, stecil read only
    RESOURCE_STATE_DEPTH_AND_STENCIL_READ            = 8,  // Depth read, stencil read
    RESOURCE_STATE_VERTEX_SHADER_RESOURCE            = 9,  // Vertex shader read only
    RESOURCE_STATE_HULL_SHADER_RESOURCE              = 10, // Hull shader read only
    RESOURCE_STATE_DOMAIN_SHADER_RESOURCE            = 11, // Domain shader read only
    RESOURCE_STATE_GEOMETRY_SHADER_RESOURCE          = 12, // Geometry shader read only
    RESOURCE_STATE_PIXEL_SHADER_RESOURCE             = 13, // Pixel shader read only
    RESOURCE_STATE_COMPUTE_SHADER_RESOURCE           = 14, // Compute shader read only
    RESOURCE_STATE_VERTEX_UNORDERED_ACCESS           = 15, // Vertex shader read/write
    RESOURCE_STATE_HULL_UNORDERED_ACCESS             = 16, // Hull shader read/write
    RESOURCE_STATE_DOMAIN_UNORDERED_ACCESS           = 17, // Domain shader read/write
    RESOURCE_STATE_GEOMETRY_UNORDERED_ACCESS         = 18, // Geometry shader read/write
    RESOURCE_STATE_PIXEL_UNORDERED_ACCESS            = 19, // Pixel shader read/write
    RESOURCE_STATE_COMPUTE_UNORDERED_ACCESS          = 21, // Compute shader read/write
    RESOURCE_STATE_TRANSFER_DST                      = 22, //
    RESOURCE_STATE_TRANSFER_SRC                      = 23, //
    RESOURCE_STATE_RESOLVE_DST                       = 24, //
    RESOURCE_STATE_RESOLVE_SRC                       = 25, //
    RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE = 26, //
    RESOURCE_STATE_PRESENT                           = 27, //
};

enum CompileResult
{
    COMPILE_SUCCESS                       = 0,
    COMPILE_ERROR_FAILED                  = -1,
    COMPILE_ERROR_UNKNOWN_LANGUAGE        = -2,
    COMPILE_ERROR_INVALID_SOURCE          = -3,
    COMPILE_ERROR_INVALID_ENTRY_POINT     = -4,
    COMPILE_ERROR_INVALID_SHADER_STAGE    = -5,
    COMPILE_ERROR_INVALID_SHADER_MODEL    = -6,
    COMPILE_ERROR_INTERNAL_COMPILER_ERROR = -7,
    COMPILE_ERROR_PREPROCESS_FAILED       = -8,
    COMPILE_ERROR_COMPILE_FAILED          = -9,
    COMPILE_ERROR_LINK_FAILED             = -10,
    COMPILE_ERROR_MAP_IO_FAILED           = -11,
    COMPILE_ERROR_CODE_GEN_FAILED         = -12,
};

struct CompilerOptions
{
    uint32_t BindingShiftTexture = 0;
    uint32_t BindingShiftUBO     = 0;
    uint32_t BindingShiftImage   = 0;
    uint32_t BindingShiftSampler = 0;
    uint32_t BindingShiftSSBO    = 0;
    uint32_t BindingShiftUAV     = 0;
};

struct VulkanRenderer
{
    bool             DebugEnabled             = true;
    bool             RayTracingEnabled        = false;
    VkInstance       Instance                 = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice           = VK_NULL_HANDLE;
    VkDevice         Device                   = VK_NULL_HANDLE;
    VmaAllocator     Allocator                = VK_NULL_HANDLE;
    VkSemaphore      DeviceFence              = VK_NULL_HANDLE;
    uint64_t         DeviceFenceValue         = 0;
    uint32_t         GraphicsQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    VkQueue          Queue                    = VK_NULL_HANDLE;
    VkSurfaceKHR     Surface                  = VK_NULL_HANDLE;
    VkSwapchainKHR   Swapchain                = VK_NULL_HANDLE;
    uint32_t         SwapchainImageCount      = 0;
    VkSemaphore      ImageReadySemaphore      = VK_NULL_HANDLE;
    VkFence          ImageReadyFence          = VK_NULL_HANDLE;
    VkSemaphore      PresentReadySemaphore    = VK_NULL_HANDLE;

    VulkanRenderer();
    ~VulkanRenderer();
};

// Command buffer container for convenience
struct CommandObjects
{
    VulkanRenderer* pRenderer     = nullptr;
    VkCommandPool   CommandPool   = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

    ~CommandObjects();
};

bool     InitVulkan(VulkanRenderer* pRenderer, bool enableDebug, bool enableRayTracing, uint32_t apiVersion = VK_API_VERSION_1_3);
bool     InitSwapchain(VulkanRenderer* pRenderer, HWND hwnd, uint32_t width, uint32_t height, uint32_t imageCount = 2);
bool     WaitForGpu(VulkanRenderer* pRenderer);
VkResult GetSwapchainImages(VulkanRenderer* pRenderer, std::vector<VkImage>& images);
VkResult AcquireNextImage(VulkanRenderer* pRenderer, uint32_t* pImageIndex);
bool     SwapchainPresent(VulkanRenderer* pRenderer, uint32_t imageIndex);

VkResult CreateCommandBuffer(VulkanRenderer* pRenderer, VkCommandPoolCreateFlags poolCreateFlags, CommandObjects* pCmdBuf);
VkResult ExecuteCommandBuffer(VulkanRenderer* pRenderer, const CommandObjects* pCmdBuf);

// This is slow
VkResult TransitionImageLayout(
    VulkanRenderer*    pRenderer,
    VkImage            image,
    uint32_t           firstMip,
    uint32_t           mipCount,
    uint32_t           firstLayer,
    uint32_t           layerCount,
    VkImageAspectFlags aspectFlags,
    ResourceState      stateBefore,
    ResourceState      stateAfter);

struct VulkanBuffer
{
    VkBuffer          Buffer;
    VmaAllocation     Allocation;
    VmaAllocationInfo AllocationInfo;
};

struct VulkanImage
{
    VkImage           Image;
    VmaAllocation     Allocation;
    VmaAllocationInfo AllocationInfo;
};

//! @fn CreateBuffer
//!
//! Creates a buffer object with memory allocated and bound.
//!
VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             srcSize,
    VkBufferUsageFlags usageFlags,
    VmaMemoryUsage     memoryUsage,
    VkDeviceSize       minAlignment, // Use 0 for no alignment
    VulkanBuffer*      pBuffer);

//! @fn CreateBuffer
//!
//! Creates a buffer object with memory allocated and bound that
//! is host visible. Source data is copied to buffer if pSrcData
//! is not NULL.
//!
VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             srcSize,
    const void*        pSrcData, // [OPTIONAL] NULL if no data
    VkBufferUsageFlags usageFlags,
    VkDeviceSize       minAlignment, // Use 0 for no alignment
    VulkanBuffer*      pBuffer);

VkResult CreateUAVBuffer(
    VulkanRenderer*    pRenderer,
    size_t             size,
    VkBufferUsageFlags usageFlags,
    VkDeviceSize       minAlignment, // Use 0 for no alignment
    VulkanBuffer*      pBuffer);

void DestroyBuffer(VulkanRenderer* pRenderer, const VulkanBuffer* pBuffer);

VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, const VulkanBuffer* pBuffer);
VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, VkAccelerationStructureKHR accelStruct);

CompileResult CompileGLSL(
    const std::string&     shaderSource,
    const std::string&     entryPoint,
    VkShaderStageFlagBits  shaderStage,
    const CompilerOptions& options,
    std::vector<uint32_t>* pSPIRV,
    std::string*           pErrorMsg);

// Loaded funtions
extern PFN_vkCreateRayTracingPipelinesKHR             fn_vkCreateRayTracingPipelinesKHR;
extern PFN_vkGetRayTracingShaderGroupHandlesKHR       fn_vkGetRayTracingShaderGroupHandlesKHR;
extern PFN_vkGetAccelerationStructureBuildSizesKHR    fn_vkGetAccelerationStructureBuildSizesKHR;
extern PFN_vkCreateAccelerationStructureKHR           fn_vkCreateAccelerationStructureKHR;
extern PFN_vkCmdBuildAccelerationStructuresKHR        fn_vkCmdBuildAccelerationStructuresKHR;
extern PFN_vkCmdTraceRaysKHR                          fn_vkCmdTraceRaysKHR;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR fn_vkGetAccelerationStructureDeviceAddressKHR;
extern PFN_vkGetDescriptorSetLayoutSizeEXT            fn_vkGetDescriptorSetLayoutSizeEXT;
extern PFN_vkGetDescriptorSetLayoutBindingOffsetEXT   fn_vkGetDescriptorSetLayoutBindingOffsetEXT;
extern PFN_vkGetDescriptorEXT                         fn_vkGetDescriptorEXT;
extern PFN_vkCmdBindDescriptorBuffersEXT              fn_vkCmdBindDescriptorBuffersEXT;
extern PFN_vkCmdSetDescriptorBufferOffsetsEXT         fn_vkCmdSetDescriptorBufferOffsetsEXT;