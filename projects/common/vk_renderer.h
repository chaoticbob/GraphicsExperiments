#pragma once

#include "config.h"

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <dxcapi.h>

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

#define GREX_DEFAULT_RTV_FORMAT VK_FORMAT_B8G8R8A8_UNORM
#define GREX_DEFAULT_DSV_FORMAT VK_FORMAT_D32_SFLOAT

struct VkMipOffset
{
    uint32_t offset    = 0;
    uint32_t rowStride = 0;
};

struct VulkanRenderer
{
    bool              DebugEnabled             = true;
    bool              RayTracingEnabled        = false;
    VkInstance        Instance                 = VK_NULL_HANDLE;
    VkPhysicalDevice  PhysicalDevice           = VK_NULL_HANDLE;
    VkDevice          Device                   = VK_NULL_HANDLE;
    VmaAllocator      Allocator                = VK_NULL_HANDLE;
    VkSemaphore       DeviceFence              = VK_NULL_HANDLE;
    uint64_t          DeviceFenceValue         = 0;
    uint32_t          GraphicsQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    VkQueue           Queue                    = VK_NULL_HANDLE;
    VkSurfaceKHR      Surface                  = VK_NULL_HANDLE;
    VkSwapchainKHR    Swapchain                = VK_NULL_HANDLE;
    uint32_t          SwapchainImageCount      = 0;
    VkImageUsageFlags SwapchainImageUsage      = 0;
    VkSemaphore       ImageReadySemaphore      = VK_NULL_HANDLE;
    VkFence           ImageReadyFence          = VK_NULL_HANDLE;
    VkSemaphore       PresentReadySemaphore    = VK_NULL_HANDLE;

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
bool     WaitForFence(VulkanRenderer* pRenderer, VkFence fence);
VkResult GetSwapchainImages(VulkanRenderer* pRenderer, std::vector<VkImage>& images);
VkResult AcquireNextImage(VulkanRenderer* pRenderer, uint32_t* pImageIndex);
bool     SwapchainPresent(VulkanRenderer* pRenderer, uint32_t imageIndex);

VkResult CreateCommandBuffer(VulkanRenderer* pRenderer, VkCommandPoolCreateFlags poolCreateFlags, CommandObjects* pCmdBuf);
void     DestroyCommandBuffer(VulkanRenderer* pRenderer, CommandObjects* pCmdBuf);
VkResult ExecuteCommandBuffer(VulkanRenderer* pRenderer, const CommandObjects* pCmdBuf, VkFence fence = VK_NULL_HANDLE);

void CmdTransitionImageLayout(
    VkCommandBuffer    cmdBuf,
    VkImage            image,
    uint32_t           firstMip,
    uint32_t           mipCount,
    uint32_t           firstLayer,
    uint32_t           layerCount,
    VkImageAspectFlags aspectFlags,
    ResourceState      stateBefore,
    ResourceState      stateAfter);

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

struct VulkanPipelineLayout
{
    VkDescriptorSetLayout DescriptorSetLayout;
    VkPipelineLayout      PipelineLayout;
};

struct VulkanBuffer
{
    VkBuffer          Buffer;
    VmaAllocation     Allocation;
    VmaAllocationInfo AllocationInfo;
    VkDeviceSize      Size;
};

struct VulkanImage
{
    VkImage           Image;
    VmaAllocation     Allocation;
    VmaAllocationInfo AllocationInfo;
};

struct VulkanAccelStruct
{
    VulkanBuffer               Buffer;
    VkAccelerationStructureKHR AccelStruct;
};

struct VulkanAttachmentInfo
{
    VkFormat            Format;
    VkAttachmentLoadOp  LoadOp;
    VkAttachmentStoreOp StoreOp;
    VkImageUsageFlags   ImageUsage;
};

// Simple render pass and framebuffer
struct VulkanRenderPass
{
    VkRenderPass  RenderPass;
    VkFramebuffer Framebuffer;
};

//! @fn CreateBuffer
//!
//! Creates a buffer object with memory allocated and bound.
//!
VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             size,
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

//! @fn CreateBuffer
//!
//! Creates a buffer object with memory allocated and bound that
//! is based on memoryUsage. Source data is copied to buffer if pSrcData
//! is not NULL.
//!
VkResult CreateBuffer(
    VulkanRenderer*    pRenderer,
    size_t             srcSize,
    const void*        pSrcData, // [OPTIONAL] NULL if no data
    VkBufferUsageFlags usageFlags,
    VmaMemoryUsage     memoryUsage,
    VkDeviceSize       minAlignment, // Use 0 for no alignment
    VulkanBuffer*      pBuffer);

/*
VkResult CreateUAVBuffer(
    VulkanRenderer*    pRenderer,
    size_t             size,
    VkDeviceSize       minAlignment, // Use 0 for no alignment
    VulkanBuffer*      pBuffer);
*/

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
    VulkanImage*      pImage);

VkResult CreateTexture(
    VulkanRenderer*                 pRenderer,
    uint32_t                        width,
    uint32_t                        height,
    VkFormat                        format,
    const std::vector<VkMipOffset>& mipOffsets,
    uint64_t                        srcSizeBytes,
    const void*                     pSrcData,
    VulkanImage*                    pImage);

VkResult CreateTexture(
    VulkanRenderer* pRenderer,
    uint32_t        width,
    uint32_t        height,
    VkFormat        format,
    uint64_t        srcSizeBytes,
    const void*     pSrcData,
    VulkanImage*    pImage);

VkResult CreateImageView(
    VulkanRenderer*    pRenderer,
    const VulkanImage* pImage,
    VkImageViewType    viewType,
    VkFormat           format,
    uint32_t           firstMipLevel,
    uint32_t           numMipLevels,
    uint32_t           firstArrayLayer,
    uint32_t           numArrayLayers,
    VkImageView*       pImageView);

VkResult CreateDSV(
    VulkanRenderer* pRenderer,
    uint32_t        width,
    uint32_t        height,
    VulkanImage*    pImage);

VkResult CreateRenderPass(
    VulkanRenderer*                          pRenderer,
    const std::vector<VulkanAttachmentInfo>& colorInfos,
    const VulkanAttachmentInfo&              depthStencilInfo,
    uint32_t                                 width,
    uint32_t                                 height,
    VulkanRenderPass*                        pRenderPass);

void DestroyBuffer(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer);

VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, const VulkanBuffer* pBuffer);
VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, VkAccelerationStructureKHR accelStruct);
VkDeviceAddress GetDeviceAddress(VulkanRenderer* pRenderer, const VulkanAccelStruct* pAccelStruct);

VkResult CreateDrawVertexColorPipeline(
    VulkanRenderer*  pRenderer,
    VkPipelineLayout pipeline_layout,
    VkShaderModule   vsShaderModule,
    VkShaderModule   fsShaderModule,
    VkFormat         rtvFormat,
    VkFormat         dsvFormat,
    VkPipeline*      pPipeline,
    VkCullModeFlags  cullMode = VK_CULL_MODE_BACK_BIT);

HRESULT CreateDrawNormalPipeline(
   VulkanRenderer*      pRenderer,
   VkPipelineLayout     pipelineLayout,
   VkShaderModule       vsShaderModule,
   VkShaderModule       fsShaderModule,
   VkFormat             rtvFormat,
   VkFormat             dsvFormat,
   VkPipeline*          pPipeline,
   bool                 enableTangents = false,
   VkCullModeFlagBits   cullMode = VK_CULL_MODE_BACK_BIT);

CompileResult CompileGLSL(
    const std::string&     shaderSource,
    const std::string&     entryPoint,
    VkShaderStageFlagBits  shaderStage,
    const CompilerOptions& options,
    std::vector<uint32_t>* pSPIRV,
    std::string*           pErrorMsg);

HRESULT CompileHLSL(
    const std::string&     shaderSource,
    const std::string&     entryPoint,
    const std::string&     profile,
    std::vector<uint32_t>* pSPIRV,
    std::string*           pErrorMsg);

// Buffer
void WriteDescriptor(
    VulkanRenderer*       pRenderer,
    void*                 pDescriptorBufferStartAddress,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t              binding,
    uint32_t              arrayElement,
    VkDescriptorType      descriptorType,
    const VulkanBuffer*   pBuffer);

// Acceleration structure
void WriteDescriptor(
    VulkanRenderer*          pRenderer,
    void*                    pDescriptorBufferStartAddress,
    VkDescriptorSetLayout    descriptorSetLayout,
    uint32_t                 binding,
    uint32_t                 arrayElement,
    const VulkanAccelStruct* pAccelStruct);

// Image view
void WriteDescriptor(
    VulkanRenderer*       pRenderer,
    void*                 pDescriptorBufferStartAddress,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t              binding,
    uint32_t              arrayElement,
    VkDescriptorType      descriptorType,
    VkImageView           imageView,
    VkImageLayout         imageLayout);

// Image view
void WriteDescriptor(
    VulkanRenderer*       pRenderer,
    void*                 pDescriptorBufferStartAddress,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t              binding,
    uint32_t              arrayElement,
    VkSampler             sampler);

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