#ifndef SCENE_H
#define SCENE_H

#include "config.h"

#define GLM_FORCE_QUAT_DATA_XYZW
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

struct SceneBuffer;
struct SceneTexture;

struct SceneIndexBufferView
{
    SceneBuffer* pBuffer = nullptr;
    uint32_t     Offset  = 0;
    uint32_t     Size    = 0;
    GREXFormat   Format  = GREX_FORMAT_UNKNOWN;
    uint32_t     Count   = 0;
};

struct SceneVertexBufferView
{
    SceneBuffer* pBuffer = nullptr;
    uint32_t     Offset  = 0;
    uint32_t     Size    = 0;
    uint32_t     Stride  = 0;
    GREXFormat   Format  = GREX_FORMAT_UNKNOWN;
};

struct SceneBuffer
{
    uint32_t Size     = 0;
    bool     Mappable = false;
};

struct SceneTexture
{
    uint32_t   Width        = 0;
    uint32_t   Height       = 0;
    uint32_t   Depth        = 0;
    GREXFormat Format       = GREX_FORMAT_UNKNOWN;
    uint32_t   NumMipLevels = 0;
};

struct ScenePrimitiveBatch
{
    SceneIndexBufferView  IndexBufferView;
    SceneVertexBufferView PositionBufferView;
    SceneVertexBufferView VertexColorBufferView;
    SceneVertexBufferView TexCoordBufferView;
    SceneVertexBufferView NormalBufferView;
    SceneVertexBufferView TangentBufferView;
};

struct SceneMesh
{
    std::vector<ScenePrimitiveBatch> Batches;
};

struct SceneNode
{
    uint32_t MeshIndex = UINT32_MAX;
    vec3     Translate = vec3(0);
    quat     Rotation  = quat(0, 0, 0, 1); // <X, Y, Z, W>
    vec3     Scale     = vec3(1);
};

struct SceneLoadOptions
{
    bool EnableVertexColors = false;
    bool EnableTexCoords    = false;
    bool EnableNormals      = false;
    bool EnableTangents     = false;
};

struct Scene
{
    std::vector<std::unique_ptr<SceneBuffer>>  Buffers;
    std::vector<std::unique_ptr<SceneTexture>> Textures;
    std::vector<SceneMesh>                     Meshes;
    std::vector<SceneNode>                     Nodes;

    virtual bool CreateBuffer(
        uint32_t      size,
        const void*   pData,
        bool          mappable,
        SceneBuffer** ppBuffer) = 0;

    virtual bool CreateTexture(
        uint32_t       width,
        uint32_t       height,
        uint32_t       depth,
        GREXFormat     format,
        uint32_t       numMipLevels,
        SceneTexture** ppTexture) = 0;

    bool LoadGLTF(const std::filesystem::path& path);
};

#endif // SCENE_H
