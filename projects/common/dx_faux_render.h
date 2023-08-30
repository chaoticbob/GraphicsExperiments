#ifndef DX_FAUX_RENDER_H
#define DX_FAUX_RENDER_H

#include "faux_render.h"
#include "dx_renderer.h"

namespace DxFauxRender
{

struct Buffer
    : public FauxRender::Buffer
{
    ComPtr<ID3D12Resource> Resource;

    virtual bool Map(void** ppData) override;
    virtual void Unmap() override;
};

struct Image
    : public FauxRender::Image
{
    ComPtr<ID3D12Resource> Resource;
};

struct SceneGraph : public FauxRender::SceneGraph
{
    DxRenderer* pRenderer = nullptr;

    struct
    {
        uint32_t Scene                 = UINT32_MAX;
        uint32_t Camera                = UINT32_MAX;
        uint32_t Draw                  = UINT32_MAX;
        uint32_t InstanceBuffer        = UINT32_MAX;
        uint32_t MaterialBuffer        = UINT32_MAX;
        uint32_t MaterialSampler       = UINT32_MAX;
        uint32_t MaterialImages        = UINT32_MAX;
        uint32_t IBLEnvMapTexture      = UINT32_MAX;
        uint32_t IBLIrrMapTexture      = UINT32_MAX;
        uint32_t IBLIntegrationLUT     = UINT32_MAX;
        uint32_t IBLMapSampler         = UINT32_MAX;
        uint32_t IBLIntegrationSampler = UINT32_MAX;
    } RootParameterIndices;

    SceneGraph(DxRenderer* pTheRenderer);

    virtual bool CreateTemporaryBuffer(
        uint32_t             size,
        const void*          pData,
        bool                 mappable,
        FauxRender::Buffer** ppBuffer) override;

    virtual void DestroyTemporaryBuffer(
        FauxRender::Buffer** ppBuffer) override;

    virtual bool CreateBuffer(
        uint32_t             bufferSize,
        uint32_t             srcSize,
        const void*          pSrcData,
        bool                 mappable,
        FauxRender::Buffer** ppBuffer) override;

    virtual bool CreateBuffer(
        FauxRender::Buffer*  pSrcBuffer,
        bool                 mappable,
        FauxRender::Buffer** ppBuffer) override;

    virtual bool CreateImage(
        const BitmapRGBA8u* pBitmap,
        FauxRender::Image** ppImage) override;

    virtual bool CreateImage(
        uint32_t                      width,
        uint32_t                      height,
        GREXFormat                    format,
        const std::vector<MipOffset>& mipOffsets,
        size_t                        srcImageDataSize,
        const void*                   pSrcImageData,
        FauxRender::Image**           ppImage) override;
};

DxFauxRender::Buffer* Cast(FauxRender::Buffer* pBuffer);
DxFauxRender::Image*  Cast(FauxRender::Image* pImage);

void Draw(const FauxRender::SceneGraph* pGraph, uint32_t instanceIndex, const FauxRender::Mesh* pMesh, ID3D12GraphicsCommandList* pCmdList);
void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, const FauxRender::SceneNode* pGeometryNode, ID3D12GraphicsCommandList* pCmdList);
void Draw(const FauxRender::SceneGraph* pGraph, const FauxRender::Scene* pScene, ID3D12GraphicsCommandList* pCmdList);

} // namespace DxFauxRender

#endif // DX_SCENE_RENDER_H
