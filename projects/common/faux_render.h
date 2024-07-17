#ifndef FAUX_RENDER_H
#define FAUX_RENDER_H

#include "config.h"
#include "bitmap.h"

#define GLM_FORCE_QUAT_DATA_XYZW
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/transform2.hpp>
using namespace glm;

namespace FauxRender
{

enum SceneNodeType
{
    SCENE_NODE_TYPE_UNKNOWN  = 0,
    SCENE_NODE_TYPE_GEOMETRY = 1,
    SCENE_NODE_TYPE_CAMERA   = 2,
    SCENE_NODE_TYPE_LIGHT    = 3,
    SCENE_NODE_TYPE_LOCATOR  = 4, // aka Empty in Blender or Null in other packages
};

enum FilterMode
{
    FILTER_MODE_NEAREST = 1,
    FILTER_MODE_LINEAR  = 1,
};

enum TextureAddressMode
{
    TEXTURE_ADDRESS_MODE_CLAMP  = 0,
    TEXTURE_ADDRESS_MODE_WRAP   = 1,
    TEXTURE_ADDRESS_MODE_MIRROR = 2,
    TEXTURE_ADDRESS_MODE_BORDER = 3,
};

struct Buffer;
struct Texture;
struct Sampler;

struct BufferView
{
    uint32_t   Offset = 0;
    uint32_t   Size   = 0;
    uint32_t   Stride = 0;
    GREXFormat Format = GREX_FORMAT_UNKNOWN;
    uint32_t   Count  = 0;
};

struct Buffer
{
    uint32_t Size     = 0;
    bool     Mappable = false;

    virtual bool Map(void** ppData) = 0;
    virtual void Unmap()            = 0;
};

struct Image
{
    std::string Name      = "";
    uint32_t    Width     = 0;
    uint32_t    Height    = 0;
    uint32_t    Depth     = 0;
    GREXFormat  Format    = GREX_FORMAT_UNKNOWN;
    uint32_t    NumLevels = 0;
    uint32_t    NumLayers = 0;
};

struct Texture
{
    std::string          Name     = "";
    FauxRender::Image*   pImage   = nullptr;
    FauxRender::Sampler* pSampler = nullptr;
};

struct Sampler
{
    std::string                    Name      = "";
    FauxRender::FilterMode         MinFilter = FILTER_MODE_NEAREST;
    FauxRender::FilterMode         MagFilter = FILTER_MODE_NEAREST;
    FauxRender::FilterMode         MipFilter = FILTER_MODE_NEAREST;
    FauxRender::TextureAddressMode AddressU  = TEXTURE_ADDRESS_MODE_CLAMP;
    FauxRender::TextureAddressMode AddressV  = TEXTURE_ADDRESS_MODE_CLAMP;
    FauxRender::TextureAddressMode AddressW  = TEXTURE_ADDRESS_MODE_CLAMP;
};

//
// NOTE: Tex coord transform is determined by looking through
//       the texture views in this order - if they exist:
//         1) PBR base color
//         2) PBR metallic roughness
//         3) Normal texture
//         4) Occlusion texture
//         5) Emissive texture
//
// From GLTF spec (material.pbrMetallicRoughness.metallicRoughnessTexture):
//   The metalness values are sampled from the B channel.
//   The roughness values are sampled from the G channel
//
struct Material
{
    std::string          Name                      = "";
    glm::vec4            BaseColor                 = {1, 1, 1, 1};
    float                MetallicFactor            = 1;
    float                RoughnessFactor           = 1;
    glm::vec3            Emissive                  = {0, 0, 0};
    float                EmissiveStrength          = 0;
    FauxRender::Texture* pBaseColorTexture         = nullptr;
    FauxRender::Texture* pMetallicRoughnessTexture = nullptr;
    FauxRender::Texture* pNormalTexture            = nullptr;
    FauxRender::Texture* pOcclusionTexture         = nullptr;
    FauxRender::Texture* pEmissiveTexture          = nullptr;
    glm::vec2            TexCoordTranslate         = {0, 0};
    float                TexCoordRotate            = 0;
    glm::vec2            TexCoordScale             = {1, 1};
}; // namespace FauxRender

struct PrimitiveBatch
{
    FauxRender::Material*  pMaterial             = nullptr;
    FauxRender::BufferView IndexBufferView       = {};
    FauxRender::BufferView PositionBufferView    = {};
    FauxRender::BufferView VertexColorBufferView = {};
    FauxRender::BufferView TexCoordBufferView    = {};
    FauxRender::BufferView NormalBufferView      = {};
    FauxRender::BufferView TangentBufferView     = {};
};

struct Mesh
{
    std::string                 Name        = "";
    std::vector<PrimitiveBatch> DrawBatches = {};
    FauxRender::Buffer*         pBuffer     = nullptr;
};

struct SceneNode
{
    std::string           Name      = "";
    SceneNodeType         Type      = SCENE_NODE_TYPE_UNKNOWN;
    uint32_t              Parent    = UINT32_MAX; // Indexes into SceneGraph::Nodes
    std::vector<uint32_t> Children  = {};         // Indexes into SceneGraph::Nodes
    FauxRender::Mesh*     pMesh     = nullptr;
    glm::vec3             Translate = vec3(0);
    glm::quat             Rotation  = quat(0, 0, 0, 1); // <X, Y, Z, W>
    glm::vec3             Scale     = vec3(1);

    struct
    {
        float AspectRatio = 1.0f;
        float FovY        = 60.0f;
        float NearClip    = 0.1f;
        float FarClip     = 10000.0f;
    } Camera;
};

struct Scene
{
    std::string                         Name          = "";
    std::vector<FauxRender::SceneNode*> Nodes         = {};
    std::vector<FauxRender::SceneNode*> GeometryNodes = {};
    const SceneNode*                    pActiveCamera = nullptr;

    FauxRender::Buffer* pCameraArgs = nullptr;

    FauxRender::Buffer* pInstanceBuffer = nullptr;
    uint32_t            NumInstances    = 0;

    uint32_t GetGeometryNodeIndex(const FauxRender::SceneNode* pGeometryNode) const;
};

struct SceneGraph
{
    std::vector<std::unique_ptr<FauxRender::Scene>>     Scenes;
    std::vector<std::unique_ptr<FauxRender::SceneNode>> Nodes;
    std::vector<std::unique_ptr<FauxRender::Mesh>>      Meshes;
    std::vector<std::unique_ptr<FauxRender::Buffer>>    Buffers;
    std::vector<std::unique_ptr<FauxRender::Material>>  Materials;
    std::vector<std::unique_ptr<FauxRender::Texture>>   Textures;
    std::vector<std::unique_ptr<FauxRender::Image>>     Images;
    std::vector<std::unique_ptr<FauxRender::Sampler>>   Samplers;
    FauxRender::Image*                                  pDefaultBaseColorImage         = nullptr;
    FauxRender::Image*                                  pDefaultMetallicRoughnessImage = nullptr;
    FauxRender::Image*                                  pDefaultNormalImage            = nullptr;
    FauxRender::Image*                                  pDefaultOcclusionImage         = nullptr;
    FauxRender::Image*                                  pDefaultEmissiveImage          = nullptr;
    FauxRender::Sampler*                                pDefaultClampedSampler         = nullptr;
    FauxRender::Sampler*                                pDefaultRepeatSampler          = nullptr;

    FauxRender::Buffer* pMaterialBuffer = nullptr;
    uint32_t            NumMaterials    = 0;

    uint32_t GetMaterialIndex(const FauxRender::Material* pMaterial) const;
    uint32_t GetImageIndex(const FauxRender::Image* pImage) const;
    uint32_t GetSamplerIndex(const FauxRender::Sampler* pSampler) const;

    virtual bool CreateTemporaryBuffer(
        uint32_t             size,
        const void*          pData,
        bool                 mappable,
        FauxRender::Buffer** ppBuffer) = 0;

    virtual void DestroyTemporaryBuffer(
        Buffer** ppBuffer) = 0;

    virtual bool CreateBuffer(
        uint32_t             bufferSize,
        uint32_t             srcSize,
        const void*          pSrcData,
        bool                 mappable,
        FauxRender::Buffer** ppBuffer) = 0;

    virtual bool CreateBuffer(
        FauxRender::Buffer*  pSrcBuffer,
        bool                 mappable,
        FauxRender::Buffer** ppBuffer) = 0;

    virtual bool CreateImage(
        const BitmapRGBA8u* pBitmap,
        FauxRender::Image** ppImage) = 0;

    virtual bool CreateImage(
        uint32_t                      width,
        uint32_t                      height,
        GREXFormat                    format,
        const std::vector<MipOffset>& mipOffsets,
        size_t                        srcImageDataSize,
        const void*                   pSrcImageData,
        FauxRender::Image**           ppImage) = 0;

    // Vulkan needs to override this since it needs to store a sampler object
    virtual bool CreateSampler(
        FauxRender::FilterMode         minFilter,
        FauxRender::FilterMode         magFilter,
        FauxRender::FilterMode         mipFilter,
        FauxRender::TextureAddressMode addressU,
        FauxRender::TextureAddressMode addressV,
        FauxRender::TextureAddressMode addressW,
        FauxRender::Sampler**          ppSampler);

    bool InitializeResources();

protected:
    bool InitializeDefaults();
};

struct LoadOptions
{
    bool EnableVertexColors = false;
    bool EnableTexCoords    = true;
    bool EnableNormals      = true;
    bool EnableTangents     = true;
};

bool LoadGLTF(const std::filesystem::path& path, const FauxRender::LoadOptions& loadOptions, FauxRender::SceneGraph* pGraph);

namespace Shader
{

using uint     = glm::uint;
using float2   = glm::vec2;
using float3   = glm::vec3;
using float4   = glm::vec4;
using float4x4 = glm::mat4;

const uint32_t MAX_INSTANCES = 100;
const uint32_t MAX_MATERIALS = 100;
const uint32_t MAX_SAMPLERS  = 32;
const uint32_t MAX_IMAGES    = 1024;

enum MaterialFlagBits
{
    MATERIAL_FLAG_BASE_COLOR_TEXTURE         = (1 << 1),
    MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE = (1 << 2),
    MATERIAL_FLAG_NORMAL_TEXTURE             = (1 << 3),
    MATERIAL_FLAG_OCCLUSION_TEXTURE          = (1 << 4),
    MATERIAL_FLAG_EMISSIVE_TEXTURE           = (1 << 5),
};

struct CameraParams
{
    float4x4 ViewProjectionMatrix;
    float3   EyePosition;

#ifdef __APPLE__
    uint32_t _padding0;
#endif // __APPLE__
};

struct DrawParams
{
    uint InstanceIndex;
    uint MaterialIndex;
};

struct InstanceParams
{
    float4x4 ModelMatrix;
    float4x4 NormalMatrix;
};

struct TextureParams
{
    uint ImageIndex;
    uint SamplerIndex;
};

struct MaterialParams
{
    uint MaterialFlags;
#ifdef __APPLE__
    uint _padding0[3];
#endif // __APPLE__
    float3 BaseColor;
#ifdef __APPLE__
    uint _padding1;
#endif // __APPLE__
    float         RoughnessFactor;
    float         MetallicFactor;
    TextureParams BaseColorTexture;
    TextureParams MetallicRoughnessTexture;
    TextureParams NormalTexture;
    TextureParams OcclusionTexture;
    TextureParams EmissiveTexture;

    vec2  TexCoordTranslate;
    vec2  TexCoordScale;
    float TexCoordRotate;
#ifdef __APPLE__
    uint padding2[3];
#endif // __APPLE__
};

} // namespace Shader

} // namespace FauxRender

#endif // FAUX_RENDER_H
