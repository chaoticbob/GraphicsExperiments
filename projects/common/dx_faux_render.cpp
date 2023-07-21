#include "dx_faux_render.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

namespace DxFauxRender
{

bool Buffer::Map(void** ppData)
{
    if (!this->Mappable)
    {
        return false;
    }

    HRESULT hr = this->Resource->Map(0, nullptr, ppData);
    if (FAILED(hr))
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

    this->Resource->Unmap(0, nullptr);
}

// =============================================================================
// SceneGraph
// =============================================================================
SceneGraph::SceneGraph(DxRenderer* pTheRenderer)
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

    D3D12_HEAP_TYPE heapType = mappable ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> resource;
    //
    HRESULT hr = ::CreateBuffer(
        this->pRenderer,
        size,
        pData,
        heapType,
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    DxFauxRender::Buffer* pBuffer = new DxFauxRender::Buffer();
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

    DxFauxRender::Buffer* pBuffer = static_cast<DxFauxRender::Buffer*>(*ppBuffer);

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

    D3D12_HEAP_TYPE heapType = mappable ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

    // Create the buffer resource
    ComPtr<ID3D12Resource> resource;
    //
    HRESULT hr = ::CreateBuffer(
        this->pRenderer,
        srcSize,
        pSrcData,
        heapType,
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    // Allocate buffer container
    auto pBuffer = new DxFauxRender::Buffer();
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

    ID3D12Resource* pSrcResource = static_cast<DxFauxRender::Buffer*>(pSrcBuffer)->Resource.Get();
    D3D12_HEAP_TYPE heapType     = mappable ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

    // Create the buffer resource
    ComPtr<ID3D12Resource> resource;
    //
    HRESULT hr = ::CreateBuffer(
        this->pRenderer,
        pSrcResource,
        heapType,
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    // Allocate buffer container
    auto pBuffer = new DxFauxRender::Buffer();
    if (IsNull(pBuffer))
    {
        return false;
    }

    // Update buffer container
    pBuffer->Size     = static_cast<uint32_t>(pSrcResource->GetDesc().Width);
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
    ComPtr<ID3D12Resource> resource;
    //
    HRESULT hr = ::CreateTexture(
        this->pRenderer,
        pBitmap->GetWidth(),
        pBitmap->GetHeight(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        pBitmap->GetSizeInBytes(),
        pBitmap->GetPixels(),
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    // Allocate image container
    auto pImage = new DxFauxRender::Image();
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

    auto dxFormat = ToDxFormat(format);
    if (dxFormat == DXGI_FORMAT_UNKNOWN)
    {
        return false;
    }

    // Create the buffer resource
    ComPtr<ID3D12Resource> resource;
    //
    HRESULT hr = ::CreateTexture(
        this->pRenderer,
        width,
        height,
        dxFormat,
        mipOffsets,
        srcImageDataSize,
        pSrcImageData,
        &resource);
    if (FAILED(hr))
    {
        return false;
    }

    // Allocate image container
    auto pImage = new DxFauxRender::Image();
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
DxFauxRender::Buffer* Cast(FauxRender::Buffer* pBuffer)
{
    return static_cast<DxFauxRender::Buffer*>(pBuffer);
}

DxFauxRender::Image* Cast(FauxRender::Image* pImage)
{
    return static_cast<DxFauxRender::Image*>(pImage);
}

void Draw(const FauxRender::SceneGraph* pGraph, uint32_t instanceIndex, const FauxRender::Mesh* pMesh, ID3D12GraphicsCommandList* pCmdList)
{
    assert((pMesh != nullptr) && "pMesh is NULL");

    const DxFauxRender::Buffer* pBuffer = DxFauxRender::Cast(pMesh->pBuffer);
    assert((pBuffer != nullptr) && "mesh's buffer is NULL");

    // Buffer VA
    D3D12_GPU_VIRTUAL_ADDRESS bufferStart = pBuffer->Resource->GetGPUVirtualAddress();

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
            D3D12_INDEX_BUFFER_VIEW view = {};
            view.BufferLocation          = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + batch.IndexBufferView.Offset);
            view.SizeInBytes             = static_cast<UINT>(batch.IndexBufferView.Size);
            view.Format                  = ToDxFormat(batch.IndexBufferView.Format);

            pCmdList->IASetIndexBuffer(&view);
        }

        // Vertex buffers
        {
            UINT                     numBufferViews                          = 0;
            D3D12_VERTEX_BUFFER_VIEW bufferViews[GREX_MAX_VERTEX_ATTRIBUTES] = {};

            // Position
            if (batch.PositionBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView          = batch.PositionBufferView;
                auto& dstView          = bufferViews[numBufferViews];
                dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
                dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
                dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);

                ++numBufferViews;
            }
            // Tex Coord
            if (batch.TexCoordBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView          = batch.TexCoordBufferView;
                auto& dstView          = bufferViews[numBufferViews];
                dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
                dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
                dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);

                ++numBufferViews;
            }
            //  Normal
            if (batch.NormalBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView          = batch.NormalBufferView;
                auto& dstView          = bufferViews[numBufferViews];
                dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
                dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
                dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);

                ++numBufferViews;
            }
            //  Tangent
            if (batch.TangentBufferView.Format != GREX_FORMAT_UNKNOWN)
            {
                auto& srcView          = batch.TangentBufferView;
                auto& dstView          = bufferViews[numBufferViews];
                dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
                dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
                dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);

                ++numBufferViews;
            }

            pCmdList->IASetVertexBuffers(0, numBufferViews, bufferViews);
        }

        // Draw root constants
        {
            FauxRender::Shader::DrawParams drawParams = {};
            drawParams.InstanceIndex                  = instanceIndex;
            drawParams.MaterialIndex                  = pGraph->GetMaterialIndex(batch.pMaterial);
            assert((drawParams.InstanceIndex != UINT32_MAX) && "drawParams.InstanceIndex is invalid");
            assert((drawParams.MaterialIndex != UINT32_MAX) && "drawParams.MaterialIndex is invalid");

            pCmdList->SetGraphicsRoot32BitConstants(
                static_cast<const DxFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.Draw,
                2,
                &drawParams,
                0);
        }

        // Draw
        pCmdList->DrawIndexedInstanced(
            /* IndexCountPerInstance */ batch.IndexBufferView.Count,
            /* InstanceCount         */ 1,
            /* StartIndexLocation    */ 0,
            /* BaseVertexLocation    */ 0,
            /* StartInstanceLocation */ 0);
    }
}

void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, const FauxRender::SceneNode* pGeometryNode, ID3D12GraphicsCommandList* pCmdList)
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

    Draw(pGraph, instanceIndex, pGeometryNode->pMesh, pCmdList);
}

void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, ID3D12GraphicsCommandList* pCmdList)
{
    assert((pScene != nullptr) && "pScene is NULL");

    // Set camera
    {
        auto resource = DxFauxRender::Cast(pScene->pCameraArgs)->Resource;
        pCmdList->SetGraphicsRootConstantBufferView(
            static_cast<const DxFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.Camera,
            resource->GetGPUVirtualAddress());
    }

    // Set instance buffer
    {
        auto resource = DxFauxRender::Cast(pScene->pInstanceBuffer)->Resource;
        pCmdList->SetGraphicsRootShaderResourceView(
            static_cast<const DxFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.InstanceBuffer,
            resource->GetGPUVirtualAddress());
    }

    // Set material buffer
    {
        auto resource = DxFauxRender::Cast(pGraph->pMaterialBuffer)->Resource;
        pCmdList->SetGraphicsRootShaderResourceView(
            static_cast<const DxFauxRender::SceneGraph*>(pGraph)->RootParameterIndices.MaterialBuffer,
            resource->GetGPUVirtualAddress());
    }

    for (auto pGeometryNode : pScene->GeometryNodes)
    {
        Draw(pGraph, pScene, pGeometryNode, pCmdList);
    }
}

} // namespace DxFauxRender
