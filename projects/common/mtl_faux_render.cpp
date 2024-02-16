#include "mtl_faux_render.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

namespace MtlFauxRender
{

const int32_t kPositionIndex  = 0;
const int32_t kTexCoordIndex  = 1;
const int32_t kNormalIndex    = 2;
const int32_t kTangentIndex   = 3;

bool Buffer::Map(void** ppData)
{
    if (!this->Mappable)
    {
        return false;
    }

    (*ppData) = this->Resource.Buffer.get()->contents();

    if ((*ppData) == nullptr)
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

    // If this were being used multiple times per frame instead of just
    // at loading, we should probably only mark a smaller chunk as "modified"
    MTL::Buffer* mtlBuffer = this->Resource.Buffer.get();
    mtlBuffer->didModifyRange(NS::Range::Make(0, mtlBuffer->length()));
}

// =============================================================================
// SceneGraph
// =============================================================================
SceneGraph::SceneGraph(MetalRenderer* pTheRenderer)
    : pRenderer(pTheRenderer)
{
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

    MetalBuffer resource;
    //
    NS::Error* pError = ::CreateBuffer(
        this->pRenderer,
        size,
        pData,
        &resource);
    if (pError != nullptr)
    {
        return false;
    }

    MtlFauxRender::Buffer* pBuffer = new MtlFauxRender::Buffer();
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

    MtlFauxRender::Buffer* pBuffer = static_cast<MtlFauxRender::Buffer*>(*ppBuffer);

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

    // Create the buffer resource
    MetalBuffer resource;
    //
    NS::Error* pError = ::CreateBuffer(
        this->pRenderer,
        srcSize,
        pSrcData,
        &resource);
    if (pError != nullptr)
    {
        return false;
    }

    // Allocate buffer container
    auto pBuffer = new MtlFauxRender::Buffer();
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

    MetalBuffer* pSrcResource = &static_cast<MtlFauxRender::Buffer*>(pSrcBuffer)->Resource;

    // Create the buffer resource
    MetalBuffer resource;
    //
    NS::Error* pError = ::CreateBuffer(
        this->pRenderer,
        pSrcResource,
        &resource);
    if (pError != nullptr)
    {
        return false;
    }

    // Allocate buffer container
    auto pBuffer = new MtlFauxRender::Buffer();
    if (IsNull(pBuffer))
    {
        return false;
    }

    // Update buffer container
    pBuffer->Size     = static_cast<uint32_t>(resource.Buffer.get()->length());
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
    MetalTexture resource;
    //
    NS::Error* pError = ::CreateTexture(
        this->pRenderer,
        pBitmap->GetWidth(),
        pBitmap->GetHeight(),
        MTL::PixelFormatRGBA8Unorm,
        pBitmap->GetSizeInBytes(),
        pBitmap->GetPixels(),
        &resource);
    if (pError != nullptr)
    {
        return false;
    }

    // Allocate image container
    auto pImage = new MtlFauxRender::Image();
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

    auto mtlFormat = ToMTLFormat(format);
    if (mtlFormat == MTL::PixelFormatInvalid)
    {
        return false;
    }

    // Create the buffer resource
    MetalTexture resource;
    //
    NS::Error* pError = ::CreateTexture(
        this->pRenderer,
        width,
        height,
        mtlFormat,
        mipOffsets,
        srcImageDataSize,
        pSrcImageData,
        &resource);
    if (pError != nullptr)
    {
        return false;
    }

    // Allocate image container
    auto pImage = new MtlFauxRender::Image();
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
MtlFauxRender::Buffer* Cast(FauxRender::Buffer* pBuffer)
{
    return static_cast<MtlFauxRender::Buffer*>(pBuffer);
}

MtlFauxRender::Image* Cast(FauxRender::Image* pImage)
{
    return static_cast<MtlFauxRender::Image*>(pImage);
}

bool CalculateVertexStrides(FauxRender::Scene* pScene, std::vector<uint32>& vertexStrides)
{
    assert((pScene != nullptr) && "pScene is NULL");

    bool          meshPartStrideMismatch = false;

    vertexStrides.resize(4);

    for (auto pGeometryNode : pScene->GeometryNodes)
    {
        assert((pGeometryNode != nullptr) && "pGeometryNode is NULL");
        assert((pGeometryNode->Type == FauxRender::SCENE_NODE_TYPE_GEOMETRY) && "node is not of drawable type");

        const FauxRender::Mesh* pMesh           = pGeometryNode->pMesh;

        for (auto& batch : pMesh->DrawBatches)
        {
            // Position
            {
                assert((batch.PositionBufferView.Format != GREX_FORMAT_UNKNOWN) && "Mesh does not contain positions!");
                uint32_t vertexStride = batch.PositionBufferView.Stride;

                meshPartStrideMismatch = meshPartStrideMismatch ||
                   (vertexStrides[kPositionIndex] != vertexStride && vertexStrides[kPositionIndex] != 0);

                vertexStrides[kPositionIndex] = vertexStride;
            }

            // Tex Coord
            {
                uint32_t texCoordStride = batch.NormalBufferView.Format != GREX_FORMAT_UNKNOWN
                   ? batch.TexCoordBufferView.Stride
                   : batch.PositionBufferView.Stride; // Can't have zero stride

                meshPartStrideMismatch = meshPartStrideMismatch ||
                   (vertexStrides[kTexCoordIndex] != texCoordStride && vertexStrides[kTexCoordIndex] != 0);

                vertexStrides[kTexCoordIndex] = texCoordStride;
            }

            // Normal
            {
                uint32_t normalStride = batch.NormalBufferView.Format != GREX_FORMAT_UNKNOWN
                   ? batch.NormalBufferView.Stride
                   : batch.PositionBufferView.Stride; // Can't have zero stride

                meshPartStrideMismatch = meshPartStrideMismatch ||
                   (vertexStrides[kNormalIndex] != normalStride && vertexStrides[kNormalIndex] != 0);

                vertexStrides[kNormalIndex] = normalStride;
            }

            // Tangent
            {
                uint32_t tangentStride = batch.TangentBufferView.Format != GREX_FORMAT_UNKNOWN
                   ? batch.TangentBufferView.Stride
                   : batch.PositionBufferView.Stride; // Can't have zero stride

                meshPartStrideMismatch = meshPartStrideMismatch ||
                   (vertexStrides[kTangentIndex] != tangentStride && vertexStrides[kTangentIndex] != 0);

                vertexStrides[kTangentIndex] = tangentStride;
            }
        }
    }

    if (meshPartStrideMismatch)
    {
        assert(false && "Not all mesh parts use the same vertex strides, which is not supported on Metal");
    }
}

void Draw(const FauxRender::SceneGraph* pGraph, uint32_t instanceIndex, const FauxRender::Mesh* pMesh, MTL::RenderCommandEncoder* pRenderEncoder)
{
    assert((pMesh != nullptr) && "pMesh is NULL");

    const MtlFauxRender::Buffer* pBuffer = MtlFauxRender::Cast(pMesh->pBuffer);
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

        // Vertex buffers
        {
            MTL::Buffer* bufferViews[GREX_MAX_VERTEX_ATTRIBUTES]   = {};
            NS::UInteger bufferOffsets[GREX_MAX_VERTEX_ATTRIBUTES] = {};

            // Position
            {
               assert(batch.PositionBufferView.Format != GREX_FORMAT_UNKNOWN);
               bufferViews[kPositionIndex]   = pBuffer->Resource.Buffer.get();
               bufferOffsets[kPositionIndex] = batch.PositionBufferView.Offset;
            }

            // Tex Coord
            {
                bufferViews[kTexCoordIndex]   = pBuffer->Resource.Buffer.get();

                bufferOffsets[kTexCoordIndex] = batch.TexCoordBufferView.Format != GREX_FORMAT_UNKNOWN
                  ? batch.TexCoordBufferView.Offset
                  : batch.PositionBufferView.Offset;
            }

            //  Normal
            {
                bufferViews[kNormalIndex]   = pBuffer->Resource.Buffer.get();

                bufferOffsets[kNormalIndex] = batch.NormalBufferView.Format != GREX_FORMAT_UNKNOWN
                  ? batch.NormalBufferView.Offset
                  : batch.PositionBufferView.Offset;
            }

            //  Tangent
            {
                bufferViews[kTangentIndex]   = pBuffer->Resource.Buffer.get();
                bufferOffsets[kTangentIndex] = batch.TangentBufferView.Format != GREX_FORMAT_UNKNOWN
                  ? batch.TangentBufferView.Offset
                  : batch.PositionBufferView.Offset;
            }

            pRenderEncoder->setVertexBuffers(bufferViews, bufferOffsets, NS::Range::Make(0, 4));
        }

        // Draw root constants
        {
            // Need struct with padding for Metal, make sure we changes this if the original changes
            assert(sizeof(FauxRender::Shader::DrawParams) == 8 && "DrawParams struct changed, please change the AlignedDrawParams version as well");

            struct AlignedDrawParams
            {
                uint32_t InstanceIndex;
                uint32_t MaterialIndex;
                uint32_t _padding0[2];
            };

            AlignedDrawParams drawParams = {};
            drawParams.InstanceIndex     = instanceIndex;
            drawParams.MaterialIndex     = pGraph->GetMaterialIndex(batch.pMaterial);
            assert((drawParams.InstanceIndex != UINT32_MAX) && "drawParams.InstanceIndex is invalid");
            assert((drawParams.MaterialIndex != UINT32_MAX) && "drawParams.MaterialIndex is invalid");

            uint32_t index = static_cast<const MtlFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.Draw;
            pRenderEncoder->setVertexBytes(&drawParams, sizeof(AlignedDrawParams), index);
            pRenderEncoder->setFragmentBytes(&drawParams, sizeof(AlignedDrawParams), index);
        }

        // Draw
        pRenderEncoder->drawIndexedPrimitives(
            /* primitiveType     */ MTL::PrimitiveType::PrimitiveTypeTriangle,
            /* indexCount        */ batch.IndexBufferView.Count,
            /* indexType         */ ToMTLIndexType(batch.IndexBufferView.Format),
            /* indexBuffer       */ pBuffer->Resource.Buffer.get(),
            /* indexBufferOffset */ batch.IndexBufferView.Offset);
    }
}

void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, const FauxRender::SceneNode* pGeometryNode, MTL::RenderCommandEncoder* pRenderEncoder)
{
    assert((pScene != nullptr) && "pScene is NULL");
    assert((pGeometryNode != nullptr) && "pGeometryNode is NULL");
    assert((pGeometryNode->Type == FauxRender::SCENE_NODE_TYPE_GEOMETRY) && "node is not of drawable type");

    uint32_t instanceIndex = pScene->GetGeometryNodeIndex(pGeometryNode);
    assert((instanceIndex != UINT32_MAX) && "instanceIndex is invalid");

    Draw(pGraph, instanceIndex, pGeometryNode->pMesh, pRenderEncoder);
}

void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, MTL::RenderCommandEncoder* pRenderEncoder)
{
    assert((pScene != nullptr) && "pScene is NULL");

    // Set camera
    {
        auto&    resource = MtlFauxRender::Cast(pScene->pCameraArgs)->Resource;
        uint32_t index    = static_cast<const MtlFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.Camera;
        pRenderEncoder->setVertexBuffer(resource.Buffer.get(), 0, index);
        pRenderEncoder->setFragmentBuffer(resource.Buffer.get(), 0, index);
    }

    // Set instance buffer
    {
        auto&    resource = MtlFauxRender::Cast(pScene->pInstanceBuffer)->Resource;
        uint32_t index    = static_cast<const MtlFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.InstanceBuffer;
        pRenderEncoder->setVertexBuffer(resource.Buffer.get(), 0, index);
        pRenderEncoder->setFragmentBuffer(resource.Buffer.get(), 0, index);
    }

    // Set material buffer
    {
        auto&    resource = MtlFauxRender::Cast(pGraph->pMaterialBuffer)->Resource;
        uint32_t index    = static_cast<const MtlFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.MaterialBuffer;
        pRenderEncoder->setVertexBuffer(resource.Buffer.get(), 0, index);
        pRenderEncoder->setFragmentBuffer(resource.Buffer.get(), 0, index);
    }

    for (auto pGeometryNode : pScene->GeometryNodes)
    {
        Draw(pGraph, pScene, pGeometryNode, pRenderEncoder);
    }
}

} // namespace MtlFauxRender
