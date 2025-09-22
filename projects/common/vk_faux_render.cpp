#include "vk_faux_render.h"

#define GLM_FORCE_QUAT_DATA_XYZW
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CAMERA_REGISTER          1  // b1
#define INSTANCE_BUFFER_REGISTER 10 // t10
#define MATERIAL_BUFFER_REGISTER 11 // t11

namespace VkFauxRender
{

bool Buffer::Map(void** ppData)
{
    if (!this->Mappable)
    {
        return false;
    }

    const VulkanBuffer* buffer = &this->Resource;
    VkResult            vkres  = vmaMapMemory(buffer->Allocator, buffer->Allocation, ppData);

    if (vkres != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

void Buffer::Unmap()
{
    if (!this->Mappable)
    {
        return;
    }

    const VulkanBuffer* buffer = &this->Resource;
    vmaUnmapMemory(buffer->Allocator, buffer->Allocation);
}

// =============================================================================
// SceneGraph
// =============================================================================
SceneGraph::SceneGraph(VulkanRenderer* pTheRenderer, VulkanPipelineLayout* pThePipelineLayout)
    : pRenderer(pTheRenderer),
      pPipelineLayout(pThePipelineLayout)
{
    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        VkDeviceSize size = 0;
        fn_vkGetDescriptorSetLayoutSizeEXT(pRenderer->Device, pPipelineLayout->DescriptorSetLayout, &size);

        VkBufferUsageFlags usageFlags =
            VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        ::CreateBuffer(
            pRenderer,  // pRenderer
            size,       // srcSize
            nullptr,    // pSrcData
            usageFlags, // usageFlags
            0,          // minAlignment
            &DescriptorBuffer);
    }

    this->InitializeDefaults();
}

bool SceneGraph::CreateTemporaryBuffer(
    uint32_t             size,
    const void*          pData,
    bool                 mappable,
    FauxRender::Buffer** ppBuffer)
{
    if ((size == 0) || IsNull(ppBuffer))
    {
        return false;
    }

    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VulkanBuffer resource;
    //
    VkResult vkres = ::CreateBuffer(
        this->pRenderer,
        size,
        pData,
        usageFlags,
        VMA_MEMORY_USAGE_GPU_TO_CPU,
        0,
        &resource);

    if (vkres != VK_SUCCESS)
    {
        return false;
    }

    VkFauxRender::Buffer* pBuffer = new VkFauxRender::Buffer();
    if (IsNull(pBuffer))
    {
        return false;
    }

    pBuffer->Size     = size;
    pBuffer->Mappable = mappable;
    pBuffer->Resource = resource;

    //
    // Don't add buffer since to SceneGraph::Buffers since it's temporary
    //

    *ppBuffer = pBuffer;

    return true;
}

void SceneGraph::DestroyTemporaryBuffer(
    FauxRender::Buffer** ppBuffer)
{
    if (IsNull(ppBuffer))
    {
        return;
    }

    VkFauxRender::Buffer* pBuffer = static_cast<VkFauxRender::Buffer*>(*ppBuffer);

    delete pBuffer;

    *ppBuffer = nullptr;
}

bool SceneGraph::CreateBuffer(
    uint32_t             bufferSize,
    uint32_t             srcSize,
    const void*          pSrcData,
    bool                 mappable,
    FauxRender::Buffer** ppBuffer)
{
    if (IsNull(ppBuffer) || (srcSize > bufferSize))
    {
        return false;
    }

    VkBufferUsageFlags usageFlags = 
       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | 
       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // Create the buffer resource
    VulkanBuffer resource;
    //
    VkResult vkres = ::CreateBuffer(
        this->pRenderer,
        srcSize,
        pSrcData,
        usageFlags,
        VMA_MEMORY_USAGE_GPU_ONLY,
        0,
        &resource);

    if (vkres != VK_SUCCESS)
    {
        return false;
    }

    // Allocate buffer container
    auto pBuffer = new VkFauxRender::Buffer();
    if (IsNull(pBuffer))
    {
        return false;
    }

    // Update buffer container
    pBuffer->Size     = srcSize;
    pBuffer->Mappable = mappable;
    pBuffer->Resource = resource;

    // Store buffer in the graph
    this->Buffers.push_back(std::move(std::unique_ptr<FauxRender::Buffer>(pBuffer)));

    // Write output pointer
    *ppBuffer = pBuffer;

    return true;
}

bool SceneGraph::CreateBuffer(
    FauxRender::Buffer*  pSrcBuffer,
    bool                 mappable,
    FauxRender::Buffer** ppBuffer)
{
    if (IsNull(pSrcBuffer) || IsNull(ppBuffer))
    {
        return false;
    }

    VulkanBuffer       pSrcResource = static_cast<VkFauxRender::Buffer*>(pSrcBuffer)->Resource;
    VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Create the buffer resource
    VulkanBuffer resource;
    //
    HRESULT hr = ::CreateBuffer(
        this->pRenderer,
        usageFlags,
        &pSrcResource,
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    // Allocate buffer container
    auto pBuffer = new VkFauxRender::Buffer();
    if (IsNull(pBuffer))
    {
        return false;
    }

    // Update buffer container
    pBuffer->Size     = static_cast<uint32_t>(pSrcResource.Size);
    pBuffer->Mappable = mappable;
    pBuffer->Resource = resource;

    // Store buffer in the graph
    this->Buffers.push_back(std::move(std::unique_ptr<FauxRender::Buffer>(pBuffer)));

    // Write output pointer
    *ppBuffer = pBuffer;

    return true;
}

bool SceneGraph::CreateImage(
    const BitmapRGBA8u* pBitmap,
    FauxRender::Image** ppImage)
{
    if (IsNull(pBitmap) || IsNull(ppImage))
    {
        return false;
    }

    // Create the buffer resource
    VulkanImage resource;
    //
    HRESULT hr = ::CreateTexture(
        this->pRenderer,
        pBitmap->GetWidth(),
        pBitmap->GetHeight(),
        VK_FORMAT_R8G8B8A8_UNORM,
        pBitmap->GetSizeInBytes(),
        pBitmap->GetPixels(),
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    // Allocate image container
    auto pImage = new VkFauxRender::Image();
    if (IsNull(pImage))
    {
        return false;
    }

    // Update image container
    pImage->Width     = pBitmap->GetWidth();
    pImage->Height    = pBitmap->GetHeight();
    pImage->Depth     = 1;
    pImage->Format    = GREX_FORMAT_R8G8B8A8_UNORM;
    pImage->NumLevels = 1;
    pImage->NumLayers = 1;
    pImage->Resource  = resource;

    // Store image in the graph
    this->Images.push_back(std::move(std::unique_ptr<FauxRender::Image>(pImage)));

    // Write output pointer
    *ppImage = pImage;

    return true;
}

bool SceneGraph::CreateImage(
    uint32_t                      width,
    uint32_t                      height,
    GREXFormat                    format,
    const std::vector<MipOffset>& mipOffsets,
    size_t                        srcImageDataSize,
    const void*                   pSrcImageData,
    FauxRender::Image**           ppImage)
{
    if (mipOffsets.empty() || (srcImageDataSize == 0) || IsNull(pSrcImageData) || IsNull(ppImage))
    {
        return false;
    }

    auto vkFormat = ToVkFormat(format);
    if (vkFormat == VK_FORMAT_UNDEFINED)
    {
        return false;
    }

    // Create the buffer resource
    VulkanImage resource;
    //
    VkResult vkres = ::CreateTexture(
        this->pRenderer,
        width,
        height,
        vkFormat,
        mipOffsets,
        srcImageDataSize,
        pSrcImageData,
        &resource);
    if (vkres != VK_SUCCESS)
    {
        return false;
    }

    // Allocate image container
    auto pImage = new VkFauxRender::Image();
    if (IsNull(pImage))
    {
        return false;
    }

    // Update image container
    pImage->Width     = width;
    pImage->Height    = height;
    pImage->Depth     = 1;
    pImage->Format    = format;
    pImage->NumLevels = static_cast<uint32_t>(mipOffsets.size());
    pImage->NumLayers = 1;
    pImage->Resource  = resource;

    // Store image in the graph
    this->Images.push_back(std::move(std::unique_ptr<FauxRender::Image>(pImage)));

    // Write output pointer
    *ppImage = pImage;

    return true;
}

// =============================================================================
// Functions
// =============================================================================
VkFauxRender::Buffer* Cast(FauxRender::Buffer* pBuffer)
{
    return static_cast<VkFauxRender::Buffer*>(pBuffer);
}

VkFauxRender::Image* Cast(FauxRender::Image* pImage)
{
    return static_cast<VkFauxRender::Image*>(pImage);
}

void Draw(const FauxRender::SceneGraph* pGraph, uint32_t instanceIndex, const FauxRender::Mesh* pMesh, CommandObjects* pCmdObjects)
{
    assert((pMesh != nullptr) && "pMesh is NULL");

    const VkFauxRender::Buffer* pBuffer = VkFauxRender::Cast(pMesh->pBuffer);
    assert((pBuffer != nullptr) && "mesh's buffer is NULL");

    const size_t numBatches = pMesh->DrawBatches.size();
    for (size_t batchIdx = 0; batchIdx < numBatches; ++batchIdx)
    {
        auto& batch = pMesh->DrawBatches[batchIdx];

        // Skip if no material
        if (IsNull(batch.pMaterial))
        {
            continue;
        }

        // Index buffer
        {
            vkCmdBindIndexBuffer(
                pCmdObjects->CommandBuffer,
                pBuffer->Resource.Buffer,
                batch.IndexBufferView.Offset,
                ToVkIndexType(batch.IndexBufferView.Format));
        }

        // Vertex buffers
        {
            // Bind the Vertex Buffer
            UINT         numBufferViews                            = 0;
            VkBuffer     bufferViews[GREX_MAX_VERTEX_ATTRIBUTES]   = {};
            VkDeviceSize bufferOffsets[GREX_MAX_VERTEX_ATTRIBUTES] = {};
            VkDeviceSize bufferSizes[GREX_MAX_VERTEX_ATTRIBUTES]   = {};
            VkDeviceSize bufferStrides[GREX_MAX_VERTEX_ATTRIBUTES] = {};

            // Position
            if (batch.PositionBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView                 = batch.PositionBufferView;
                bufferViews[numBufferViews]   = pBuffer->Resource.Buffer;
                bufferOffsets[numBufferViews] = srcView.Offset;
                bufferSizes[numBufferViews]   = srcView.Size;
                bufferStrides[numBufferViews] = srcView.Stride;

                ++numBufferViews;
            }
            // Tex Coord
            if (batch.TexCoordBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView                 = batch.TexCoordBufferView;
                bufferViews[numBufferViews]   = pBuffer->Resource.Buffer;
                bufferOffsets[numBufferViews] = srcView.Offset;
                bufferSizes[numBufferViews]   = srcView.Size;
                bufferStrides[numBufferViews] = srcView.Stride;

                ++numBufferViews;
            }
            //  Normal
            if (batch.NormalBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView                 = batch.NormalBufferView;
                bufferViews[numBufferViews]   = pBuffer->Resource.Buffer;
                bufferOffsets[numBufferViews] = srcView.Offset;
                bufferSizes[numBufferViews]   = srcView.Size;
                bufferStrides[numBufferViews] = srcView.Stride;

                ++numBufferViews;
            }
            //  Tangent
            if (batch.TangentBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView                 = batch.TangentBufferView;
                bufferViews[numBufferViews]   = pBuffer->Resource.Buffer;
                bufferOffsets[numBufferViews] = srcView.Offset;
                bufferSizes[numBufferViews]   = srcView.Size;
                bufferStrides[numBufferViews] = srcView.Stride;

                ++numBufferViews;
            }

            vkCmdBindVertexBuffers2(pCmdObjects->CommandBuffer, 0, 4, bufferViews, bufferOffsets, bufferSizes, bufferStrides);
        }

        // Draw root constants
        {
            FauxRender::Shader::DrawParams drawParams = {};
            drawParams.InstanceIndex                  = instanceIndex;
            drawParams.MaterialIndex                  = pGraph->GetMaterialIndex(batch.pMaterial);
            assert((drawParams.InstanceIndex != UINT32_MAX) && "drawParams.InstanceIndex is invalid");
            assert((drawParams.MaterialIndex != UINT32_MAX) && "drawParams.MaterialIndex is invalid");

            vkCmdPushConstants(
                pCmdObjects->CommandBuffer,
                reinterpret_cast<const VkFauxRender::SceneGraph*>(pGraph)->pPipelineLayout->PipelineLayout,
                VK_SHADER_STAGE_ALL_GRAPHICS,
                0,
                sizeof(FauxRender::Shader::DrawParams),
                &drawParams);
        }

        // Draw
        vkCmdDrawIndexed(
            /* Command Buffer */ pCmdObjects->CommandBuffer,
            /* indexCount     */ batch.IndexBufferView.Count,
            /* instanceCount  */ 1,
            /* firstIndex     */ 0,
            /* vertexOffset   */ 0,
            /* firstInstance  */ 0);
    }
}

void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, const FauxRender::SceneNode* pGeometryNode, CommandObjects* pCmdObjects)
{
    assert((pScene != nullptr) && "pScene is NULL");
    assert((pGeometryNode != nullptr) && "pGeometryNode is NULL");
    assert((pGeometryNode->Type == FauxRender::SCENE_NODE_TYPE_GEOMETRY) && "node is not of drawable type");

    // mat4 Rmat     = glm::toMat4(pGeometryNode->Rotation);
    // mat4 modelMat = glm::translate(pGeometryNode->Translate) * Rmat * glm::scale(pGeometryNode->Scale);

    // pCmdList->SetGraphicsRoot32BitConstants(0, 16, &modelMat, 0);
    // pCmdList->SetGraphicsRoot32BitConstants(0, 16, &Rmat, 32);

    uint32_t instanceIndex = pScene->GetGeometryNodeIndex(pGeometryNode);
    assert((instanceIndex != UINT32_MAX) && "instanceIndex is invalid");

    Draw(pGraph, instanceIndex, pGeometryNode->pMesh, pCmdObjects);
}

void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, CommandObjects* pCmdObjects)
{
    assert((pScene != nullptr) && "pScene is NULL");

    const VkFauxRender::SceneGraph* pVkGraph  = static_cast<const VkFauxRender::SceneGraph*>(pGraph);
    VulkanRenderer*                 pRenderer = pVkGraph->pRenderer;

    if (pRenderer->Features.EnableDescriptorBuffer)
    {
        void* pDescriptorBufferStartAddress = nullptr;
        vmaMapMemory(
            pRenderer->Allocator,
            pVkGraph->DescriptorBuffer.Allocation,
            &pDescriptorBufferStartAddress);

        // Set camera
        {
            auto resource = VkFauxRender::Cast(pScene->pCameraArgs)->Resource;

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                pVkGraph->pPipelineLayout->DescriptorSetLayout,
                CAMERA_REGISTER, // binding
                0,               // arrayElement
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                &resource);
        }

        // Set instance buffer
        {
            auto resource = VkFauxRender::Cast(pScene->pInstanceBuffer)->Resource;

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                pVkGraph->pPipelineLayout->DescriptorSetLayout,
                INSTANCE_BUFFER_REGISTER, // binding
                0,                        // arrayElement
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &resource);
        }

        // Set material buffer
        {
            auto resource = VkFauxRender::Cast(pGraph->pMaterialBuffer)->Resource;

            WriteDescriptor(
                pRenderer,
                pDescriptorBufferStartAddress,
                pVkGraph->pPipelineLayout->DescriptorSetLayout,
                MATERIAL_BUFFER_REGISTER, // binding
                0,                        // arrayElement
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &resource);
        }

        vmaUnmapMemory(pRenderer->Allocator, pVkGraph->DescriptorBuffer.Allocation);

        // Bind all descriptors to the command list
        VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
        descriptorBufferBindingInfo.pNext                            = nullptr;
        descriptorBufferBindingInfo.address                          = GetDeviceAddress(pRenderer, &pVkGraph->DescriptorBuffer);
        descriptorBufferBindingInfo.usage                            = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
        fn_vkCmdBindDescriptorBuffersEXT(pCmdObjects->CommandBuffer, 1, &descriptorBufferBindingInfo);

        uint32_t     bufferIndices           = 0;
        VkDeviceSize descriptorBufferOffsets = 0;
        fn_vkCmdSetDescriptorBufferOffsetsEXT(
            pCmdObjects->CommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pVkGraph->pPipelineLayout->PipelineLayout,
            0, // firstSet
            1, // setCount
            &bufferIndices,
            &descriptorBufferOffsets);
    }
    else
    {
        // Set camera
        VulkanBufferDescriptor sceneCameraDescriptor;
        {
            auto resource = VkFauxRender::Cast(pScene->pCameraArgs)->Resource;

            CreateDescriptor(
                pRenderer,
                &sceneCameraDescriptor,
                CAMERA_REGISTER, // binding
                0,               // arrayElement
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                &resource);
        }

        // Set instance buffer
        VulkanBufferDescriptor sceneInstanceBufferDescriptor;
        {
            auto resource = VkFauxRender::Cast(pScene->pInstanceBuffer)->Resource;

            CreateDescriptor(
                pRenderer,
                &sceneInstanceBufferDescriptor,
                INSTANCE_BUFFER_REGISTER, // binding
                0,                        // arrayElement
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &resource);
        }

        // Set material buffer
        VulkanBufferDescriptor sceneMaterialBufferDescriptor;
        {
            auto resource = VkFauxRender::Cast(pGraph->pMaterialBuffer)->Resource;

            CreateDescriptor(
                pRenderer,
                &sceneMaterialBufferDescriptor,
                MATERIAL_BUFFER_REGISTER, // binding
                0,                        // arrayElement
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                &resource);
        }

        // Empty Material Sampler descriptors - Required for DescriptorSet validation
        VkSamplerCreateInfo emptySamplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

        VkSampler emptySampler = VK_NULL_HANDLE;
        vkCreateSampler(pRenderer->Device, &emptySamplerInfo, nullptr, &emptySampler);

        VulkanImageDescriptor emptyMaterialSamplersDescriptors(FauxRender::Shader::MAX_SAMPLERS);
        for (int i = 0; i < FauxRender::Shader::MAX_SAMPLERS; ++i)
        {
            CreateDescriptor(
                pRenderer,
                &emptyMaterialSamplersDescriptors,
                FauxRender::Shader::MATERIAL_SAMPLER_START_REGISTER, // binding
                i,                                                   // arrayElement
                emptySampler);
        }

        // Empty Material Image descriptors - Required for DescriptorSet validation
        VulkanImageDescriptor emptyMaterialImagesDescriptors(FauxRender::Shader::MAX_IMAGES);
        CreateDescriptor(
            pRenderer,
            &emptyMaterialImagesDescriptors,
            FauxRender::Shader::MATERIAL_IMAGES_START_REGISTER, // binding
            0,                                                  // arrayElement
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        std::vector<VkDescriptorSetLayoutBinding> setLayoutBinding =
            {
                sceneCameraDescriptor.layoutBinding,
                sceneInstanceBufferDescriptor.layoutBinding,
                sceneMaterialBufferDescriptor.layoutBinding,
                emptyMaterialSamplersDescriptors.layoutBinding,
                emptyMaterialImagesDescriptors.layoutBinding,
            };

        std::vector<VkWriteDescriptorSet> writeDescriptorSets =
            {
                sceneCameraDescriptor.writeDescriptorSet,
                sceneInstanceBufferDescriptor.writeDescriptorSet,
                sceneMaterialBufferDescriptor.writeDescriptorSet,
                emptyMaterialSamplersDescriptors.writeDescriptorSet,
                emptyMaterialImagesDescriptors.writeDescriptorSet,
            };

        VulkanDescriptorSet sceneDescriptors;
        CreateAndUpdateDescriptorSet(pRenderer, setLayoutBinding, writeDescriptorSets, &sceneDescriptors);

        // Bind all descriptors to the command list
        vkCmdBindDescriptorSets(
            pCmdObjects->CommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pVkGraph->pPipelineLayout->PipelineLayout,
            0, // firstSet
            1, // setCount
            &sceneDescriptors.DescriptorSet,
            0,
            nullptr);
    }

    for (auto pGeometryNode : pScene->GeometryNodes)
    {
        Draw(pGraph, pScene, pGeometryNode, pCmdObjects);
    }
}

} // namespace VkFauxRender
