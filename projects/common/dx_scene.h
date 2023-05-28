#ifndef DX_SCENE_H
#define DX_SCENE_H

#include "scene.h"
#include "dx_renderer.h"

struct DxSceneBuffer
    : public SceneBuffer
{
    ComPtr<ID3D12Resource> Buffer;
};

struct DxSceneTexture
    : public SceneTexture
{
    ComPtr<ID3D12Resource> Texture;
};

struct DxScene
    : public Scene
{
    DxRenderer* pRenderer = nullptr;

    DxScene(DxRenderer* pTheRenderer)
        : pRenderer(pTheRenderer) {}

    virtual bool CreateBuffer(
        uint32_t      size,
        const void*   pData,
        bool          mappable,
        SceneBuffer** ppBuffer) override;

    virtual bool CreateTexture(
        uint32_t       width,
        uint32_t       height,
        uint32_t       depth,
        GREXFormat     format,
        uint32_t       numMipLevels,
        SceneTexture** ppTexture) override;

    void DrawNode(const SceneNode& node, ID3D12GraphicsCommandList* pCmdList) const;
};

#endif // DX_SCENE_H
