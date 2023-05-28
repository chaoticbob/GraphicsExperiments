#include "dx_scene.h"

bool DxScene::CreateBuffer(
    uint32_t      size,
    const void*   pData,
    bool          mappable,
    SceneBuffer** ppBuffer)
{
    if ((size == 0) || IsNull(ppBuffer)) {
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
    if (FAILED(hr)) {
        return false;
    }

    DxSceneBuffer* pBuffer = new DxSceneBuffer();
    if (IsNull(pBuffer)) {
        return false;
    }

    pBuffer->Size     = size;
    pBuffer->Mappable = mappable;
    pBuffer->Buffer   = resource;

    this->Buffers.push_back(std::move(std::unique_ptr<SceneBuffer>(pBuffer)));

    *ppBuffer = pBuffer;

    return true;
}

bool DxScene::CreateTexture(
    uint32_t       width,
    uint32_t       height,
    uint32_t       depth,
    GREXFormat     format,
    uint32_t       numMipLevels,
    SceneTexture** ppTexture)
{
    if ((width == 0) || (height == 0) || (depth == 0) || IsNull(ppTexture)) {
        return false;
    }

    DXGI_FORMAT dxFormat = ToDxFormat(format);
    if (dxFormat != DXGI_FORMAT_UNKNOWN) {
        return false;
    }

    ComPtr<ID3D12Resource> resource;
    //
    HRESULT hr = ::CreateTexture(
        this->pRenderer,
        width,
        height,
        dxFormat,
        numMipLevels,
        1,
        &resource);
    if (FAILED(hr)) {
        return false;
    }

    DxSceneTexture* pTexture = new DxSceneTexture();
    if (IsNull(pTexture)) {
        return false;
    }

    this->Textures.push_back(std::move(std::unique_ptr<SceneTexture>(pTexture)));

    pTexture->Width        = width;
    pTexture->Height       = height;
    pTexture->Depth        = depth;
    pTexture->Format       = format;
    pTexture->NumMipLevels = numMipLevels;
    pTexture->Texture      = resource;

    return true;
}

void DxScene::DrawNode(const SceneNode& node, ID3D12GraphicsCommandList* pCmdList) const
{
    const size_t numMeshes = this->Meshes.size();
    assert((node.MeshIndex < numMeshes) && "node's mesh index exceeds scene's mesh count");

    auto& mesh = this->Meshes[node.MeshIndex];

    const size_t numBatches = mesh.Batches.size();
    for (size_t batchIdx = 0; batchIdx < numBatches; ++batchIdx) {
        auto& batch = mesh.Batches[batchIdx];

        // Index buffer
        {
            D3D12_GPU_VIRTUAL_ADDRESS bufferStart = static_cast<const DxSceneBuffer*>(batch.IndexBufferView.pBuffer)->Buffer->GetGPUVirtualAddress();

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
            if (batch.PositionBufferView.Format != GREX_FORMAT_UNKNOWN) {
                auto& srcView = batch.PositionBufferView;

                D3D12_GPU_VIRTUAL_ADDRESS bufferStart = static_cast<const DxSceneBuffer*>(srcView.pBuffer)->Buffer->GetGPUVirtualAddress();

                auto& dstView          = bufferViews[numBufferViews];
                dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
                dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
                dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);

                ++numBufferViews;
            }
            //// Tex Coord
            //if (batch.TexCoordBufferView.Format != GREX_FORMAT_UNKNOWN) {
            //    auto& srcView = batch.TexCoordBufferView;
            //
            //    D3D12_GPU_VIRTUAL_ADDRESS bufferStart = static_cast<const DxSceneBuffer*>(srcView.pBuffer)->Buffer->GetGPUVirtualAddress();
            //
            //    auto& dstView          = bufferViews[numBufferViews];
            //    dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
            //    dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
            //    dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);
            //
            //    ++numBufferViews;
            //}
            // Normal
            if (batch.NormalBufferView.Format != GREX_FORMAT_UNKNOWN) {
                auto& srcView = batch.NormalBufferView;

                D3D12_GPU_VIRTUAL_ADDRESS bufferStart = static_cast<const DxSceneBuffer*>(srcView.pBuffer)->Buffer->GetGPUVirtualAddress();

                auto& dstView          = bufferViews[numBufferViews];
                dstView.BufferLocation = static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(bufferStart + srcView.Offset);
                dstView.SizeInBytes    = static_cast<UINT>(srcView.Size);
                dstView.StrideInBytes  = static_cast<UINT>(srcView.Stride);

                ++numBufferViews;
            }

            pCmdList->IASetVertexBuffers(0, numBufferViews, bufferViews);
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