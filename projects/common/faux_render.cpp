#include "faux_render.h"
#include "cgltf.h"

#include <cstring>
#include <unordered_map>

namespace FauxRender
{

// =============================================================================
//  Code
// =============================================================================

struct BufferCopyRange
{
    cgltf_buffer* pGltfBuffer  = nullptr;
    uint32_t      GltfOffset   = 0;
    uint32_t      TargetOffset = 0;
    uint32_t      Size         = 0;
};

struct BufferInfo
{
    uint32_t                     BufferSize = 0;
    std::vector<BufferCopyRange> CopyRanges = {};
};

struct LoaderInternals
{
    std::filesystem::path                                            gltfPath     = "";
    FauxRender::SceneGraph*                                          pTargetGraph = nullptr;
    std::unordered_map<const cgltf_mesh*, FauxRender::Mesh*>         MeshMap;
    std::unordered_map<const cgltf_material*, FauxRender::Material*> MaterialMap;
    std::unordered_map<FauxRender::Mesh*, BufferInfo>                MeshBufferInfo;
    std::unordered_map<const cgltf_texture*, FauxRender::Texture*>   TextureMap;
    std::unordered_map<const cgltf_image*, FauxRender::Image*>       ImageMap;
    std::unordered_map<const cgltf_sampler*, FauxRender::Sampler*>   SamplerMap;
};

static GREXFormat ToGREXFormat(const cgltf_accessor* pAccessor)
{
    if (IsNull(pAccessor))
    {
        return GREX_FORMAT_UNKNOWN;
    }

    // clang-format off
    switch (pAccessor->type) {
        default: return GREX_FORMAT_UNKNOWN;

        case cgltf_type_scalar: {
            switch (pAccessor->component_type) {
                default: return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8   : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8u  : return GREX_FORMAT_R8_UINT;
                case cgltf_component_type_r_16  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16u : return GREX_FORMAT_R16_UINT;
                case cgltf_component_type_r_32u : return GREX_FORMAT_R32_UINT;
                case cgltf_component_type_r_32f : return GREX_FORMAT_R32_FLOAT;
            }
        } break;

        case cgltf_type_vec2: {
            switch (pAccessor->component_type) {
                default: return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8   : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8u  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32f : return GREX_FORMAT_R32G32_FLOAT;
            }
        } break;

        case cgltf_type_vec3: {
            switch (pAccessor->component_type) {
                default: return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8   : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8u  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32f : return GREX_FORMAT_R32G32B32_FLOAT;
            }
        } break;

        case cgltf_type_vec4: {
            switch (pAccessor->component_type) {
                default: return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8   : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_8u  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16  : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_16u : return GREX_FORMAT_R16G16B16A16_UINT;
                case cgltf_component_type_r_32u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32f : return GREX_FORMAT_R32G32B32A32_FLOAT;
            }
        } break;
    }
    // clang-format on

    return GREX_FORMAT_UNKNOWN;
}

// =============================================================================
// Scene
// =============================================================================
uint32_t Scene::GetGeometryNodeIndex(const FauxRender::SceneNode* pGeometryNode) const
{
    auto it = std::find_if(
        this->GeometryNodes.begin(),
        this->GeometryNodes.end(),
        [pGeometryNode](const auto& elem) -> bool {
            bool match = (elem == pGeometryNode);
            return match;
        });
    if (it != this->GeometryNodes.end())
    {
        auto index = std::distance(this->GeometryNodes.begin(), it);
        return static_cast<uint32_t>(index);
    }
    return UINT32_MAX;
}

// =============================================================================
// SceneGraph
// =============================================================================
bool SceneGraph::InitializeDefaults()
{
    // Defafault base color image
    {
        const uint8_t          pixel[4]   = {0xFF, 0x00, 0xFF, 0x00};
        std::vector<MipOffset> mipOffsets = {
            MipOffset{0, sizeof(pixel)}
        };

        bool res = this->CreateImage(1, 1, GREX_FORMAT_R8G8B8A8_UNORM, mipOffsets, sizeof(pixel), pixel, &this->pDefaultBaseColorImage);
        if (!res)
        {
            assert(false && "failed to create default base color image");
            return false;
        }
    }

    // Default metallic roughness image
    {
        const uint8_t          pixel[4]   = {0xFF, 0x00, 0x00, 0x00};
        std::vector<MipOffset> mipOffsets = {
            MipOffset{0, sizeof(pixel)}
        };

        bool res = this->CreateImage(1, 1, GREX_FORMAT_R8G8B8A8_UNORM, mipOffsets, sizeof(pixel), pixel, &this->pDefaultMetallicRoughnessImage);
        if (!res)
        {
            assert(false && "failed to create default ARM image");
            return false;
        }
    }

    // Default normal image
    {
        const uint8_t          pixel[4]   = {0x00, 0x00, 0x00, 0x00};
        std::vector<MipOffset> mipOffsets = {
            MipOffset{0, sizeof(pixel)}
        };

        bool res = this->CreateImage(1, 1, GREX_FORMAT_R8G8B8A8_UNORM, mipOffsets, sizeof(pixel), pixel, &this->pDefaultNormalImage);
        if (!res)
        {
            assert(false && "failed to create default normal image");
            return false;
        }
    }

    // Default occlusion image
    {
        const uint8_t          pixel[4]   = {0xFF, 0xFF, 0xFF, 0x00};
        std::vector<MipOffset> mipOffsets = {
            MipOffset{0, sizeof(pixel)}
        };

        bool res = this->CreateImage(1, 1, GREX_FORMAT_R8G8B8A8_UNORM, mipOffsets, sizeof(pixel), pixel, &this->pDefaultOcclusionImage);
        if (!res)
        {
            assert(false && "failed to create default normal image");
            return false;
        }
    }

    // Default emissive image
    {
        const uint8_t          pixel[4]   = {0x00, 0x00, 0x00, 0x00};
        std::vector<MipOffset> mipOffsets = {
            MipOffset{0, sizeof(pixel)}
        };

        bool res = this->CreateImage(1, 1, GREX_FORMAT_R8G8B8A8_UNORM, mipOffsets, sizeof(pixel), pixel, &this->pDefaultEmissiveImage);
        if (!res)
        {
            assert(false && "failed to create default emissive image");
            return false;
        }
    }

    // Default clamped sampler
    {
        bool res = this->CreateSampler(
            FauxRender::FILTER_MODE_LINEAR,
            FauxRender::FILTER_MODE_LINEAR,
            FauxRender::FILTER_MODE_LINEAR,
            FauxRender::TEXTURE_ADDRESS_MODE_CLAMP,
            FauxRender::TEXTURE_ADDRESS_MODE_CLAMP,
            FauxRender::TEXTURE_ADDRESS_MODE_CLAMP,
            &this->pDefaultClampedSampler);
        if (!res)
        {
            assert(false && "failed to create default clamped sampler");
            return false;
        }
    }

    // Default repeat sampler
    {
        bool res = this->CreateSampler(
            FauxRender::FILTER_MODE_LINEAR,
            FauxRender::FILTER_MODE_LINEAR,
            FauxRender::FILTER_MODE_LINEAR,
            FauxRender::TEXTURE_ADDRESS_MODE_WRAP,
            FauxRender::TEXTURE_ADDRESS_MODE_WRAP,
            FauxRender::TEXTURE_ADDRESS_MODE_WRAP,
            &this->pDefaultRepeatSampler);
        if (!res)
        {
            assert(false && "failed to create default clamped sampler");
            return false;
        }
    }

    return true;
}

static mat4 CalculateTranformMatrix(const FauxRender::SceneNode* pNode)
{
    mat4 T           = glm::translate(pNode->Translate);
    mat4 R           = glm::toMat4(pNode->Rotation);
    mat4 S           = glm::scale(pNode->Scale);
    mat4 xformMatrix = T * R * S;
    return xformMatrix;
}

static mat4 EvaluateTransforMatrix(const FauxRender::SceneNode* pNode, const FauxRender::SceneGraph* pGraph)
{
    mat4 parentMatrix = mat4(1);
    if (pNode->Parent != UINT32_MAX)
    {
        auto pParentNode = pGraph->Nodes[pNode->Parent].get();
        parentMatrix     = EvaluateTransforMatrix(pParentNode, pGraph);
    }
    auto xformMatrix     = CalculateTranformMatrix(pNode);
    auto evaluatedMatrix = parentMatrix * xformMatrix;
    return evaluatedMatrix;
}

bool SceneGraph::InitializeResources()
{
    // Camera args
    for (size_t sceneIdx = 0; sceneIdx < this->Scenes.size(); ++sceneIdx)
    {
        auto pScene = this->Scenes[sceneIdx].get();

        Shader::CameraParams args = {};
        // Fill out camera args
        if (!IsNull(pScene->pActiveCamera))
        {
            auto pCameraNode = pScene->pActiveCamera;

            vec3 eyePosition   = pCameraNode->Translate;
            vec3 lookDirection = glm::toMat4(pCameraNode->Rotation) * vec4(0, 0, -1, 0);
            vec3 center        = eyePosition + lookDirection;

            mat4 viewMat     = glm::lookAt(eyePosition, center, vec3(0, 1, 0));
            mat4 projMat     = glm::perspective(pCameraNode->Camera.FovY, pCameraNode->Camera.AspectRatio, pCameraNode->Camera.NearClip, pCameraNode->Camera.FarClip);
            mat4 viewProjMat = projMat * viewMat;

            args.ViewProjectionMatrix = viewProjMat;
            args.EyePosition          = eyePosition;
        }

        // Buffer size
        const uint32_t bufferSize = Align<uint32_t>(sizeof(Shader::CameraParams), 256);

        // Create buffer and populate data
        bool res = this->CreateBuffer(
            bufferSize,                   // bufferSize
            sizeof(Shader::CameraParams), // srcSize
            &args,                        // pSrcData
            true,                         // mappable
            &pScene->pCameraArgs);        // ppBuffer
        if (!res)
        {
            assert(false && "failed to create buffer for camera args");
            return false;
        }
    }

    // Instance buffer
    for (size_t sceneIdx = 0; sceneIdx < this->Scenes.size(); ++sceneIdx)
    {
        auto pScene = this->Scenes[sceneIdx].get();

        std::vector<Shader::InstanceParams> instanceBufferData;
        for (size_t nodeIdx = 0; nodeIdx < pScene->GeometryNodes.size(); ++nodeIdx)
        {
            auto pNode    = pScene->GeometryNodes[nodeIdx];
            mat4 modelMat = EvaluateTransforMatrix(pNode, this);

            Shader::InstanceParams params = {};
            params.ModelMatrix            = modelMat;
            params.NormalMatrix           = mat4(mat3(modelMat));

            instanceBufferData.push_back(params);
        }

        // Buffer size
        const uint32_t bufferSize = static_cast<uint32_t>(SizeInBytes(instanceBufferData));

        // Create buffer and populate data
        bool res = this->CreateBuffer(
            bufferSize,                  // bufferSize
            bufferSize,                  // srcSize
            DataPtr(instanceBufferData), // pSrcData
            true,                        // mappable
            &pScene->pInstanceBuffer);   // ppBuffer
        if (!res)
        {
            assert(false && "failed to create buffer for instances");
            return false;
        }
    }

    // Material buffer
    {
        std::vector<Shader::MaterialParams> materialBufferData;
        for (size_t materialIdx = 0; materialIdx < this->Materials.size(); ++materialIdx)
        {
            auto pMaterial = this->Materials[materialIdx].get();

            Shader::MaterialParams params   = {};
            params.MaterialFlags            = 0;
            params.BaseColor                = pMaterial->BaseColor;
            params.MetallicFactor           = pMaterial->MetallicFactor;
            params.RoughnessFactor          = pMaterial->RoughnessFactor;
            params.BaseColorTexture         = {0, 0};
            params.MetallicRoughnessTexture = {0, 0};
            params.NormalTexture            = {0, 0};
            params.EmissiveTexture          = {0, 0};
            params.TexCoordTranslate        = {0, 0};
            params.TexCoordRotate           = 0;
            params.TexCoordScale            = {1, 1};

            if (!IsNull(pMaterial->pBaseColorTexture))
            {
                params.MaterialFlags |= Shader::MATERIAL_FLAG_BASE_COLOR_TEXTURE;
                params.BaseColorTexture.ImageIndex   = this->GetImageIndex(pMaterial->pBaseColorTexture->pImage);
                params.BaseColorTexture.SamplerIndex = this->GetSamplerIndex(this->pDefaultRepeatSampler);

                if (params.BaseColorTexture.ImageIndex == UINT32_MAX)
                {
                    params.BaseColorTexture.ImageIndex = this->GetImageIndex(this->pDefaultBaseColorImage);
                }
            }

            if (!IsNull(pMaterial->pMetallicRoughnessTexture))
            {
                params.MaterialFlags |= Shader::MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE;
                params.MetallicRoughnessTexture.ImageIndex   = this->GetImageIndex(pMaterial->pMetallicRoughnessTexture->pImage);
                params.MetallicRoughnessTexture.SamplerIndex = this->GetSamplerIndex(this->pDefaultRepeatSampler);

                if (params.MetallicRoughnessTexture.ImageIndex == UINT32_MAX)
                {
                    params.MetallicRoughnessTexture.ImageIndex = this->GetImageIndex(this->pDefaultMetallicRoughnessImage);
                }
            }

            if (!IsNull(pMaterial->pNormalTexture))
            {
                params.MaterialFlags |= Shader::MATERIAL_FLAG_NORMAL_TEXTURE;
                params.NormalTexture.ImageIndex   = this->GetImageIndex(pMaterial->pNormalTexture->pImage);
                params.NormalTexture.SamplerIndex = this->GetSamplerIndex(this->pDefaultRepeatSampler);

                if (params.NormalTexture.ImageIndex == UINT32_MAX)
                {
                    params.NormalTexture.ImageIndex = this->GetImageIndex(this->pDefaultNormalImage);
                }
            }

            // UV transform
            params.TexCoordTranslate = pMaterial->TexCoordTranslate;
            params.TexCoordRotate    = pMaterial->TexCoordRotate;
            params.TexCoordScale     = pMaterial->TexCoordScale;

            materialBufferData.push_back(params);
        }

        // Buffer size
        const uint32_t bufferSize = static_cast<uint32_t>(SizeInBytes(materialBufferData));

        // Create buffer and populate data
        bool res = this->CreateBuffer(
            bufferSize,                  // bufferSize
            bufferSize,                  // srcSize
            DataPtr(materialBufferData), // pSrcData
            true,                        // mappable
            &this->pMaterialBuffer);     // ppBuffer
        if (!res)
        {
            assert(false && "failed to create buffer for instances");
            return false;
        }
    }

    return true;
}

uint32_t SceneGraph::GetMaterialIndex(const FauxRender::Material* pMaterial) const
{
    auto it = std::find_if(
        this->Materials.begin(),
        this->Materials.end(),
        [pMaterial](const auto& elem) -> bool {
            bool match = (elem.get() == pMaterial);
            return match;
        });
    if (it != this->Materials.end())
    {
        auto index = std::distance(this->Materials.begin(), it);
        return static_cast<uint32_t>(index);
    }
    return UINT32_MAX;
}

uint32_t SceneGraph::GetImageIndex(const FauxRender::Image* pImage) const
{
    auto it = std::find_if(
        this->Images.begin(),
        this->Images.end(),
        [pImage](const auto& elem) -> bool {
            bool match = (elem.get() == pImage);
            return match;
        });
    if (it != this->Images.end())
    {
        auto index = std::distance(this->Images.begin(), it);
        return static_cast<uint32_t>(index);
    }
    return UINT32_MAX;
}

uint32_t SceneGraph::GetSamplerIndex(const FauxRender::Sampler* pSampler) const
{
    auto it = std::find_if(
        this->Samplers.begin(),
        this->Samplers.end(),
        [pSampler](const auto& elem) -> bool {
            bool match = (elem.get() == pSampler);
            return match;
        });
    if (it != this->Samplers.end())
    {
        auto index = std::distance(this->Samplers.begin(), it);
        return static_cast<uint32_t>(index);
    }
    return UINT32_MAX;
}

bool SceneGraph::CreateSampler(
    FauxRender::FilterMode         minFilter,
    FauxRender::FilterMode         magFilter,
    FauxRender::FilterMode         mipFilter,
    FauxRender::TextureAddressMode addressU,
    FauxRender::TextureAddressMode addressV,
    FauxRender::TextureAddressMode addressW,
    FauxRender::Sampler**          ppSampler)
{
    if (IsNull(ppSampler))
    {
        return false;
    }

    auto object = std::make_unique<FauxRender::Sampler>();
    if (!object)
    {
        return false;
    }

    object->MinFilter = minFilter;
    object->MagFilter = magFilter;
    object->MipFilter = mipFilter;
    object->AddressU  = addressU;
    object->AddressV  = addressV;
    object->AddressW  = addressW;

    *ppSampler = object.get();

    this->Samplers.push_back(std::move(object));

    return true;
}

static bool LoadGLTFMesh(
    LoaderInternals*               pInternals,
    const FauxRender::LoadOptions& loadOptions,
    const cgltf_data*              pGltfData,
    const cgltf_mesh*              pGltfMesh,
    FauxRender::Mesh*              pTargetMesh)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfMesh) || IsNull(pTargetMesh))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Name
    pTargetMesh->Name = !IsNull(pGltfMesh->name) ? pGltfMesh->name : "";
    GREX_LOG_INFO("    Loading mesh: " << pTargetMesh->Name);

    // Mesh's buffer info
    auto& targetBufferInfo = pInternals->MeshBufferInfo[pTargetMesh];

    // Buffer size
    uint32_t& targetBufferSize = targetBufferInfo.BufferSize;
    targetBufferSize           = 0;

    // Primitives
    for (size_t gltfPrimIdx = 0; gltfPrimIdx < pGltfMesh->primitives_count; ++gltfPrimIdx)
    {
        // GLTF primitive
        auto& gltfPrim = pGltfMesh->primitives[gltfPrimIdx];

        // GLTF Material
        auto& pGltfMaterial = gltfPrim.material;

        // Target batch
        pTargetMesh->DrawBatches.push_back({});
        auto& targetBatch = pTargetMesh->DrawBatches.back();

        // Material
        //
        // @TODO: Do we neeed a default material for objects
        //        that don't have a material?
        //
        if (!IsNull(pGltfMaterial))
        {
            FauxRender::Material* pTargetMaterial = nullptr;
            auto                  it              = pInternals->MaterialMap.find(gltfPrim.material);
            if (it != pInternals->MaterialMap.end())
            {
                pTargetMaterial = (*it).second;
            }
            else
            {
                auto targetMaterial = std::make_unique<FauxRender::Material>();
                if (!targetMaterial)
                {
                    assert(false && "allocation failed: FauxRender::Material");
                    return false;
                }

                // Get pointer to target material
                pTargetMaterial = targetMaterial.get();

                // Update map
                pInternals->MaterialMap[pGltfMaterial] = pTargetMaterial;

                // Add target material to graph
                pTargetGraph->Materials.push_back(std::move(targetMaterial));
            }

            targetBatch.pMaterial = pTargetMaterial;
        }

        // Index data
        {
            // Data chunks should be on 16 byte alignment
            targetBufferSize = Align<uint32_t>(targetBufferSize, 16);

            const auto& pGltfIndexData   = gltfPrim.indices;
            const auto& pGltfBufferView  = pGltfIndexData->buffer_view;
            auto&       targetBufferView = targetBatch.IndexBufferView;

            // Target format
            auto targetFormat = ToGREXFormat(pGltfIndexData);
            assert((targetFormat != GREX_FORMAT_UNKNOWN) && "invalid position attribute format");

            // Fill out destination buffer view
            const uint32_t gltfStride = static_cast<uint32_t>(pGltfIndexData->stride);
            const uint32_t gltfCount  = static_cast<uint32_t>(pGltfIndexData->count);
            //
            targetBufferView.Offset = targetBufferSize;
            targetBufferView.Size   = gltfCount * gltfStride;
            targetBufferView.Stride = gltfStride;
            targetBufferView.Format = targetFormat;
            targetBufferView.Count  = gltfCount;

            // Increment destination buffer offset
            targetBufferSize += targetBufferView.Size;

            // Build copy range
            const uint32_t gltfBufferViewOffset = static_cast<uint32_t>(pGltfBufferView->offset);
            const uint32_t gltfAccessorOffset   = static_cast<uint32_t>(pGltfIndexData->offset);
            //
            BufferCopyRange copyRange = {};
            copyRange.pGltfBuffer     = pGltfBufferView->buffer;
            copyRange.GltfOffset      = gltfBufferViewOffset + gltfAccessorOffset;
            copyRange.TargetOffset    = targetBufferView.Offset;
            copyRange.Size            = targetBufferView.Size;

            // Add copy range
            targetBufferInfo.CopyRanges.push_back(copyRange);
        }

        // Vertex data
        for (size_t gltfAttrIdx = 0; gltfAttrIdx < gltfPrim.attributes_count; ++gltfAttrIdx)
        {
            // Data chunks should be on 16 byte alignment
            targetBufferSize = Align<uint32_t>(targetBufferSize, 16);

            const auto& gltfAttr          = gltfPrim.attributes[gltfAttrIdx];
            const auto& pGltfVertexData   = gltfAttr.data;
            const auto& pGltfBufferView   = pGltfVertexData->buffer_view;
            BufferView* pTargetBufferView = nullptr;

            // Target format
            auto targetFormat = ToGREXFormat(pGltfVertexData);
            assert((targetFormat != GREX_FORMAT_UNKNOWN) && "invalid position attribute format");

            // Determine attribute
            switch (gltfAttr.type)
            {
                default:
                {
                    assert(false && "unsupported attribute type");
                }
                break;

                case cgltf_attribute_type_position:
                {
                    pTargetBufferView = &targetBatch.PositionBufferView;
                    // assert((dstFormat != GREX_FORMAT_UNKNOWN) && "invalid position attribute format");
                }
                break;

                case cgltf_attribute_type_normal:
                {
                    if (loadOptions.EnableNormals)
                    {
                        pTargetBufferView = &targetBatch.NormalBufferView;
                        // assert((dstFormat != GREX_FORMAT_UNKNOWN) && "invalid normal attribute format");
                    }
                }
                break;

                case cgltf_attribute_type_tangent:
                {
                    if (loadOptions.EnableTangents)
                    {
                        pTargetBufferView = &targetBatch.TangentBufferView;
                        // assert((dstFormat != GREX_FORMAT_UNKNOWN) && "invalid tangent attribute format");
                    }
                }
                break;

                case cgltf_attribute_type_texcoord:
                {
                    if (loadOptions.EnableTexCoords)
                    {
                        pTargetBufferView = &targetBatch.TexCoordBufferView;
                        // assert((dstFormat != GREX_FORMAT_UNKNOWN) && "invalid tex coord attribute format");
                    }
                }
                break;

                case cgltf_attribute_type_color:
                {
                    if (loadOptions.EnableVertexColors)
                    {
                        pTargetBufferView = &targetBatch.VertexColorBufferView;
                        // assert((dstFormat != GREX_FORMAT_UNKNOWN) && "invalid vertex color attribute format");
                    }
                }
                break;
            }

            // Attributes that aren'te enabled by calling code will get get skipped.
            if (!IsNull(pTargetBufferView))
            {
                // Fill out destination buffer view
                const uint32_t gltfStride = static_cast<uint32_t>(pGltfVertexData->stride);
                const uint32_t gltfCount  = static_cast<uint32_t>(pGltfVertexData->count);
                //
                pTargetBufferView->Offset = targetBufferSize;
                pTargetBufferView->Size   = gltfCount * gltfStride;
                pTargetBufferView->Stride = gltfStride;
                pTargetBufferView->Format = targetFormat;
                pTargetBufferView->Count  = gltfCount;

                // Increment destination buffer offset
                targetBufferSize += pTargetBufferView->Size;

                // Build copy range
                const uint32_t gltfBufferViewOffset = static_cast<uint32_t>(pGltfBufferView->offset);
                const uint32_t gltfAccessorOffset   = static_cast<uint32_t>(pGltfVertexData->offset);
                //
                BufferCopyRange copyRange = {};
                copyRange.pGltfBuffer     = pGltfBufferView->buffer;
                copyRange.GltfOffset      = gltfBufferViewOffset + gltfAccessorOffset;
                copyRange.TargetOffset    = pTargetBufferView->Offset;
                copyRange.Size            = pTargetBufferView->Size;

                // Add copy range
                targetBufferInfo.CopyRanges.push_back(copyRange);
            }
        }
    }

    return true;
}

static bool LoadGLTFMeshGeometryData(
    LoaderInternals*    pInternals,
    FauxRender::Buffer* pStagingBuffer,
    const cgltf_data*   pGltfData,
    const BufferInfo&   targetBufferInfo,
    FauxRender::Mesh*   pTargetMesh)
{
    if (IsNull(pStagingBuffer) || IsNull(pGltfData) || IsNull(pTargetMesh))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Map staging buffer
    char* pDstData = nullptr;
    bool  res      = pStagingBuffer->Map(reinterpret_cast<void**>(&pDstData));
    if (!res)
    {
        assert(false && "create staging buffer failed!");
        return false;
    }

    // Copy geometry data to staging buffer
    for (size_t rangeIdx = 0; rangeIdx < targetBufferInfo.CopyRanges.size(); ++rangeIdx)
    {
        const auto& copyRange   = targetBufferInfo.CopyRanges[rangeIdx];
        const auto& pGltfBuffer = copyRange.pGltfBuffer;
        const char* pSrcData    = static_cast<const char*>(pGltfBuffer->data);

        const char* pSrcAddress = pSrcData + copyRange.GltfOffset;
        char*       pDstAddress = pDstData + copyRange.TargetOffset;

        memcpy(pDstAddress, pSrcAddress, copyRange.Size);
    }

    // Unmap staging buffer
    pStagingBuffer->Unmap();

    // Create and copy data to buffer for target mesh
    FauxRender::Buffer* pTargetBuffer = nullptr;
    //
    res = pTargetGraph->CreateBuffer(pStagingBuffer, false, &pTargetBuffer);
    if (!res)
    {
        return false;
    }

    // Update target mesh's buffer
    pTargetMesh->pBuffer = pTargetBuffer;

    return true;
}

static bool LoadGLTFGeometryData(LoaderInternals* pInternals, const cgltf_data* pGltfData)
{
    if (IsNull(pInternals) || IsNull(pGltfData))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Try to reduce possible fragmentation
    //
    // Create a staging buffer that's 128MB. This should handle
    // the majority of the cases. If not it will get reallocated.
    //
    const uint32_t      kStagingBufferSize = 128 * 1024 * 1024;
    FauxRender::Buffer* pStagingBuffer     = nullptr;
    bool                res                = pTargetGraph->CreateTemporaryBuffer(kStagingBufferSize, nullptr, true, &pStagingBuffer);
    if (!res)
    {
        assert(false && "create staging buffer failed!");
        return false;
    }

    // Create target mesh buffers
    for (auto iter : pInternals->MeshBufferInfo)
    {
        auto        pTargetMesh      = iter.first;
        const auto& targetBufferInfo = iter.second;

        // Reallocate staging buffer if buffer size is too large.
        //
        // This is hacky and can potentially exhaust GPU memory.
        //
        // TODO: Change to handle copies use chunks of kStagingBufferSize if
        //       target buffer exceeds kStagingBufferSize.
        //
        if (targetBufferInfo.BufferSize > pStagingBuffer->Size)
        {
            // Destroy current staging buffer
            pTargetGraph->DestroyTemporaryBuffer(&pStagingBuffer);

            // Allocate new staging buffer
            bool res = pTargetGraph->CreateTemporaryBuffer(targetBufferInfo.BufferSize, nullptr, true, &pStagingBuffer);
            if (!res)
            {
                assert(false && "create staging buffer failed!");
                return false;
            }
        }

        // Load mesh's geometry data
        bool res = LoadGLTFMeshGeometryData(pInternals, pStagingBuffer, pGltfData, targetBufferInfo, pTargetMesh);
        if (!res)
        {
            return false;
        }
    }

    // Destroy staging buffer
    pTargetGraph->DestroyTemporaryBuffer(&pStagingBuffer);

    return true;
}

static bool LoadGLTFImageBitmap(
    LoaderInternals*    pInternals,
    const cgltf_data*   pGltfData,
    const cgltf_image*  pGltfImage,
    FauxRender::Image** ppTargetImage)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfImage) || IsNull(ppTargetImage))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    BitmapRGBA8u bitmap = {};
    //
    if (!IsNull(pGltfImage->buffer_view))
    {
        auto* pGltfBufferView = pGltfImage->buffer_view;
        auto* pGltfBuffer     = pGltfBufferView->buffer;

        auto        srcDataSize = pGltfBufferView->size;
        const char* pSrcData    = static_cast<const char*>(pGltfBuffer->data) + pGltfBufferView->offset;

        bool res = BitmapRGBA8u::Load(srcDataSize, pSrcData, &bitmap);
        if (!res)
        {
            assert(false && "image load from memory failed");
            return false;
        }
    }
    else if (!IsNull(pGltfImage->uri))
    {
        const auto parentPath = pInternals->gltfPath.parent_path();
        const auto uriPath    = parentPath / pGltfImage->uri;

        bool res = BitmapRGBA8u::Load(uriPath, &bitmap);
        if (!res)
        {
            assert(false && "image load from file failed");
            return false;
        }
    }
    else
    {
        assert(false && "invalid image data source");
        return false;
    }

    // @TODO: Add mip map generation

    // Create the target image
    FauxRender::Image* pTargetImage = nullptr;
    //
    bool res = pTargetGraph->CreateImage(&bitmap, &pTargetImage);
    if (!res)
    {
        assert(false && "create image failed");
        return false;
    }

    // Assign output
    *ppTargetImage = pTargetImage;

    return true;
}

static bool LoadGLTFImage(
    LoaderInternals*    pInternals,
    const cgltf_data*   pGltfData,
    const cgltf_image*  pGltfImage,
    FauxRender::Image** ppTargetImage)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfImage) || IsNull(ppTargetImage))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Look up image
    auto it = pInternals->ImageMap.find(pGltfImage);
    if (it != pInternals->ImageMap.end())
    {
        // Image found, assign target image, and return
        *ppTargetImage = (*it).second;
        return true;
    }

    // No image found - so create one
    std::string name = !IsNull(pGltfImage->name) ? pGltfImage->name : "";
    GREX_LOG_INFO("    Loading image: " << name);

    // Get mime type
    std::string gltfMimeType = !IsNull(pGltfImage->mime_type) ? pGltfImage->mime_type : "";

    // Target image
    FauxRender::Image* pTargetImage = nullptr;

    // KTX image data
    if (gltfMimeType == "image/ktx2")
    {
        // We no longer support the KTX file format
        return false;
    }
    // PNG, JPG, etc image data
    else
    {
        bool res = LoadGLTFImageBitmap(pInternals, pGltfData, pGltfImage, &pTargetImage);
        if (!res)
        {
            return false;
        }
    }

    assert((pTargetImage != nullptr) && "pTargetImage is NULL");

    // Update image name
    pTargetImage->Name = name;

    // Update map
    pInternals->ImageMap[pGltfImage] = pTargetImage;

    // Assign target image
    *ppTargetImage = pTargetImage;

    return true;
}

static bool LoadGLTFSampler(
    LoaderInternals*      pInternals,
    const cgltf_data*     pGltfData,
    const cgltf_sampler*  pGltfSampler,
    FauxRender::Sampler** ppTargetSampler)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfSampler) || IsNull(ppTargetSampler))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Look up sampler
    auto it = pInternals->SamplerMap.find(pGltfSampler);
    if (it != pInternals->SamplerMap.end())
    {
        // Sampler found, assign target sampler, and return
        *ppTargetSampler = (*it).second;
        return true;
    }

    // No sampler found - so create one
    auto targetSampler = std::make_unique<FauxRender::Sampler>();
    if (!targetSampler)
    {
        assert(false && "allocation failed: FauxRender::Sampler");
        return false;
    }

    // Get pointer to target mesh
    auto pTargetSampler = targetSampler.get();

    // Update map
    pInternals->SamplerMap[pGltfSampler] = pTargetSampler;

    // Add target sampler to graph
    pTargetGraph->Samplers.push_back(std::move(targetSampler));

    // @TODO: Set sampler values
    {
        pTargetSampler->Name = !IsNull(pGltfSampler->name) ? pGltfSampler->name : "";
        GREX_LOG_INFO("    Loading sampler: " << pTargetSampler->Name);
    }

    // Assign target sampler
    *ppTargetSampler = pTargetSampler;

    return true;
}

static bool LoadGLTFTexture(
    LoaderInternals*          pInternals,
    const cgltf_data*         pGltfData,
    const cgltf_texture_view* pGltfTextureView,
    FauxRender::Texture**     ppTargetTexture)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfTextureView) || IsNull(ppTargetTexture))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // GLTF texture
    auto pGltfTexture = pGltfTextureView->texture;

    // Look up texture
    auto it = pInternals->TextureMap.find(pGltfTexture);
    if (it != pInternals->TextureMap.end())
    {
        // Texture found, assign target texture, and return
        *ppTargetTexture = (*it).second;
        return true;
    }

    // No texture found - so create one
    auto targetTexture = std::make_unique<FauxRender::Texture>();
    if (!targetTexture)
    {
        assert(false && "allocation failed: FauxRender::Texture");
        return false;
    }

    // Get pointer to target mesh
    auto pTargetTexture = targetTexture.get();

    // Update map
    pInternals->TextureMap[pGltfTexture] = pTargetTexture;

    // Add target sampler to graph
    pTargetGraph->Textures.push_back(std::move(targetTexture));

    // @TODO: Set texture values
    {
        pTargetTexture->Name = !IsNull(pGltfTexture->name) ? pGltfTexture->name : "";
        GREX_LOG_INFO("    Loading texture: " << pTargetTexture->Name);
    }

    // Load image
    {
        auto pGltfImage = pGltfTexture->has_basisu ? pGltfTexture->basisu_image : pGltfTexture->image;
        if (IsNull(pGltfImage))
        {
            assert("GLTF image data missing!");
            return false;
        }

        bool res = LoadGLTFImage(pInternals, pGltfData, pGltfImage, &pTargetTexture->pImage);
        if (!res)
        {
            return false;
        }
    }

    // Load sampler
    if (!IsNull(pGltfTexture->sampler))
    {
        bool res = LoadGLTFSampler(pInternals, pGltfData, pGltfTexture->sampler, &pTargetTexture->pSampler);
        if (!res)
        {
            return false;
        }
    }

    // Assign target texture
    *ppTargetTexture = pTargetTexture;

    return true;
}

static void CopyGLTFTexCoordTransform(
    const cgltf_texture_view* pGltfTextureView,
    bool&                     hasTexCoordTransform,
    FauxRender::Material*     pTargetMaterial)
{
    if (!hasTexCoordTransform && pGltfTextureView->has_transform)
    {
        pTargetMaterial->TexCoordTranslate = {pGltfTextureView->transform.offset[0], pGltfTextureView->transform.offset[1]};
        pTargetMaterial->TexCoordRotate    = pGltfTextureView->transform.rotation;
        pTargetMaterial->TexCoordScale     = {pGltfTextureView->transform.scale[0], pGltfTextureView->transform.scale[1]};

        hasTexCoordTransform = true;
    }
}

static bool LoadGLTFMaterial(
    LoaderInternals*      pInternals,
    const cgltf_data*     pGltfData,
    const cgltf_material* pGltfMaterial,
    FauxRender::Material* pTargetMaterial)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfMaterial) || IsNull(pTargetMaterial))
    {
        return false;
    }

    // Name
    pTargetMaterial->Name = !IsNull(pGltfMaterial->name) ? pGltfMaterial->name : "";
    GREX_LOG_INFO("    Loading material: " << pTargetMaterial->Name);

    // Tex coord transform flag
    bool hasTexCoordTransform = false;

    // PBR metallic roughness
    if (pGltfMaterial->has_pbr_metallic_roughness)
    {
        auto& gltfPbr = pGltfMaterial->pbr_metallic_roughness;

        pTargetMaterial->BaseColor = glm::vec4(
            gltfPbr.base_color_factor[0],
            gltfPbr.base_color_factor[1],
            gltfPbr.base_color_factor[2],
            gltfPbr.base_color_factor[3]);

        pTargetMaterial->MetallicFactor  = gltfPbr.metallic_factor;
        pTargetMaterial->RoughnessFactor = gltfPbr.roughness_factor;

        // Base color texture
        if (!IsNull(gltfPbr.base_color_texture.texture))
        {
            bool res = LoadGLTFTexture(pInternals, pGltfData, &gltfPbr.base_color_texture, &pTargetMaterial->pBaseColorTexture);
            if (!res)
            {
                return false;
            }

            CopyGLTFTexCoordTransform(&gltfPbr.base_color_texture, hasTexCoordTransform, pTargetMaterial);
        }

        // Metallic roughness texture
        if (!IsNull(gltfPbr.metallic_roughness_texture.texture))
        {
            bool res = LoadGLTFTexture(pInternals, pGltfData, &gltfPbr.metallic_roughness_texture, &pTargetMaterial->pMetallicRoughnessTexture);
            if (!res)
            {
                return false;
            }

            CopyGLTFTexCoordTransform(&gltfPbr.metallic_roughness_texture, hasTexCoordTransform, pTargetMaterial);
        }
    }

    // Normal texture
    if (!IsNull(pGltfMaterial->normal_texture.texture))
    {
        bool res = LoadGLTFTexture(pInternals, pGltfData, &pGltfMaterial->normal_texture, &pTargetMaterial->pNormalTexture);
        if (!res)
        {
            return false;
        }

        CopyGLTFTexCoordTransform(&pGltfMaterial->normal_texture, hasTexCoordTransform, pTargetMaterial);
    }

    // Occlusion texture
    if (!IsNull(pGltfMaterial->occlusion_texture.texture))
    {
        bool res = LoadGLTFTexture(pInternals, pGltfData, &pGltfMaterial->occlusion_texture, &pTargetMaterial->pOcclusionTexture);
        if (!res)
        {
            return false;
        }

        CopyGLTFTexCoordTransform(&pGltfMaterial->occlusion_texture, hasTexCoordTransform, pTargetMaterial);
    }

    // Emissive
    pTargetMaterial->Emissive = glm::vec3(
        pGltfMaterial->emissive_factor[0],
        pGltfMaterial->emissive_factor[1],
        pGltfMaterial->emissive_factor[2]);

    // Emissive strength
    if (pGltfMaterial->has_emissive_strength)
    {
        pTargetMaterial->EmissiveStrength = pGltfMaterial->emissive_strength.emissive_strength;
    }

    // Emissive texture
    if (!IsNull(pGltfMaterial->emissive_texture.texture))
    {
        bool res = LoadGLTFTexture(pInternals, pGltfData, &pGltfMaterial->emissive_texture, &pTargetMaterial->pEmissiveTexture);
        if (!res)
        {
            return false;
        }

        CopyGLTFTexCoordTransform(&pGltfMaterial->emissive_texture, hasTexCoordTransform, pTargetMaterial);
    }

    return true;
}

static bool LoadGLTFNode(LoaderInternals* pInternals, const cgltf_data* pGltfData, const cgltf_node* pGltfNode, FauxRender::SceneNode* pTargetNode)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pGltfNode) || IsNull(pTargetNode))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Name
    pTargetNode->Name = !IsNull(pGltfNode->name) ? pGltfNode->name : "";
    GREX_LOG_INFO("    Loading node: " << pTargetNode->Name);

    // Type
    //
    // cgltf doesn't seem to have any sort of node type
    // so we need to filter using the pointers.
    //
    if (!IsNull(pGltfNode->mesh))
    {
        pTargetNode->Type = SCENE_NODE_TYPE_GEOMETRY;
    }
    else if (!IsNull(pGltfNode->camera))
    {
        pTargetNode->Type = SCENE_NODE_TYPE_CAMERA;
    }
    else if (!IsNull(pGltfNode->light))
    {
        pTargetNode->Type = SCENE_NODE_TYPE_LIGHT;
    }
    else
    {
        pTargetNode->Type = SCENE_NODE_TYPE_LOCATOR;
    }

    // Parent - if there is one
    if (!IsNull(pGltfNode->parent))
    {
        pTargetNode->Parent = static_cast<uint32_t>(cgltf_node_index(pGltfData, pGltfNode->parent));
    }

    // Children indices - remember these are local to the scene
    for (size_t childIterIdx = 0; childIterIdx < pGltfNode->children_count; ++childIterIdx)
    {
        const auto pGltfChildNode   = pGltfNode->children[childIterIdx];
        auto       gltfChildNodeIdx = cgltf_node_index(pGltfData, pGltfChildNode);
        pTargetNode->Children.push_back(static_cast<uint32_t>(gltfChildNodeIdx));
    }

    // Tranform
    {
        pTargetNode->Translate = glm::vec3(0);
        if (pGltfNode->has_translation)
        {
            float x                = pGltfNode->translation[0];
            float y                = pGltfNode->translation[1];
            float z                = pGltfNode->translation[2];
            pTargetNode->Translate = glm::vec3(x, y, z);
        }

        pTargetNode->Rotation = glm::quat(0, 0, 0, 1);
        if (pGltfNode->has_rotation)
        {
            float x               = pGltfNode->rotation[0];
            float y               = pGltfNode->rotation[1];
            float z               = pGltfNode->rotation[2];
            float w               = pGltfNode->rotation[3];
            pTargetNode->Rotation = glm::quat(x, y, z, w);
        }

        pTargetNode->Scale = glm::vec3(1);
        if (pGltfNode->has_scale)
        {
            float x            = pGltfNode->scale[0];
            float y            = pGltfNode->scale[1];
            float z            = pGltfNode->scale[2];
            pTargetNode->Scale = glm::vec3(x, y, z);
        }
    }

    switch (pTargetNode->Type)
    {
        default: assert(false && "unrecognized target node type"); break;

        case SCENE_NODE_TYPE_GEOMETRY:
        {
            FauxRender::Mesh* pTargetMesh = nullptr;
            // Look to see if there's a corresponding FauxRender::Mesh for the GLTF mesh
            auto it = pInternals->MeshMap.find(pGltfNode->mesh);
            if (it != pInternals->MeshMap.end())
            {
                // We found a mesh!
                pTargetMesh = (*it).second;
            }
            else
            {
                // Allocate target mesh
                auto targetMesh = std::make_unique<FauxRender::Mesh>();
                if (!targetMesh)
                {
                    assert(false && "allocation failed: FauxRender::Mesh");
                    return false;
                }

                // Get pointer to target mesh
                pTargetMesh = targetMesh.get();

                // Update map
                pInternals->MeshMap[pGltfNode->mesh] = pTargetMesh;

                // Add target mesh to graph
                pTargetGraph->Meshes.push_back(std::move(targetMesh));
            }

            pTargetNode->pMesh = pTargetMesh;
        }
        break;

        case SCENE_NODE_TYPE_CAMERA:
        {
            if (pGltfNode->camera->type == cgltf_camera_type_perspective)
            {
                auto& gltfCamera = pGltfNode->camera->data.perspective;
                // Aspect ratio
                if (gltfCamera.has_aspect_ratio)
                {
                    pTargetNode->Camera.AspectRatio = gltfCamera.aspect_ratio;
                }
                // Vertical FOV
                pTargetNode->Camera.FovY = gltfCamera.yfov;
                // Near clip
                pTargetNode->Camera.NearClip = gltfCamera.znear;
                if (gltfCamera.has_zfar)
                {
                    pTargetNode->Camera.FarClip = gltfCamera.zfar;
                }
                else
                {
                    pTargetNode->Camera.FarClip = gltfCamera.znear + 100.0f;
                }
            }
            else
            {
                assert(false && "unsupported camera type");
                return false;
            }
        }
        break;

        case SCENE_NODE_TYPE_LIGHT: break;
        case SCENE_NODE_TYPE_LOCATOR: break;
    }

    return true;
}

static bool LoadGLTFSceneNodes(LoaderInternals* pInternals, const cgltf_data* pGltfData, const cgltf_node* pGltfNode, FauxRender::Scene* pTargetScene)
{
    if (IsNull(pGltfData) || IsNull(pGltfNode) || IsNull(pTargetScene))
    {
        return false;
    }

    auto pTargetGraph = pInternals->pTargetGraph;

    // Process children first
    if (pGltfNode->children_count > 0)
    {
        for (size_t childIterIdx = 0; childIterIdx < pGltfNode->children_count; ++childIterIdx)
        {
            auto pGltfChildNode = pGltfNode->children[childIterIdx];

            bool res = LoadGLTFSceneNodes(pInternals, pGltfData, pGltfChildNode, pTargetScene);
            if (!res)
            {
                return false;
            }
        }
    }

    // Add node to scene
    {
        // Node index
        const size_t nodeIndex = cgltf_node_index(pGltfData, pGltfNode);

        // Target node
        auto pTargetNode = pTargetGraph->Nodes[nodeIndex].get();

        // Store faux node pointer to scene
        pTargetScene->Nodes.push_back(pTargetNode);

        // Store geometry node to scene to reduce searching later
        if (pTargetNode->Type == FauxRender::SCENE_NODE_TYPE_GEOMETRY)
        {
            pTargetScene->GeometryNodes.push_back(pTargetNode);
        }

        // Active camera
        if (IsNull(pTargetScene->pActiveCamera) && (pTargetNode->Type == SCENE_NODE_TYPE_CAMERA))
        {
            pTargetScene->pActiveCamera = pTargetNode;
        }
    }

    return true;
}

static bool LoadGLTFScene(LoaderInternals* pInternals, const cgltf_data* pGltfData, const cgltf_scene& gltfScene, FauxRender::Scene* pTargetScene)
{
    if (IsNull(pInternals) || IsNull(pGltfData) || IsNull(pTargetScene))
    {
        return false;
    }

    // auto pTargetGraph = pInternals->pTargetGraph;

    // Name
    pTargetScene->Name = !IsNull(gltfScene.name) ? gltfScene.name : "";
    GREX_LOG_INFO("  Loading scene: " << pTargetScene->Name);
    GREX_LOG_INFO("    Num nodes: " << gltfScene.nodes_count);

    // Nodes
    for (size_t nodeIterIdx = 0; nodeIterIdx < gltfScene.nodes_count; ++nodeIterIdx)
    {
        const auto pGltfNode = gltfScene.nodes[nodeIterIdx];

        bool res = LoadGLTFSceneNodes(pInternals, pGltfData, pGltfNode, pTargetScene);
        if (!res)
        {
            return false;
        }
    }

    return true;
}

bool LoadGLTF(const std::filesystem::path& path, const FauxRender::LoadOptions& loadOptions, FauxRender::SceneGraph* pTargetGraph)
{
    if (!std::filesystem::exists(path) || IsNull(pTargetGraph))
    {
        return false;
    }

    GREX_LOG_INFO("Loading GLTF: " << path);

    cgltf_options gltfOptions = {};
    cgltf_data*   pGltfData   = nullptr;

    // Parse
    cgltf_result cgres = cgltf_parse_file(
        &gltfOptions,
        path.string().c_str(),
        &pGltfData);
    if (cgres != cgltf_result_success)
    {
        return false;
    }

    // Internal loader scratch data
    LoaderInternals internals = {};
    internals.gltfPath        = path;
    internals.pTargetGraph    = pTargetGraph;

    // Load nodes
    for (size_t nodeIdx = 0; nodeIdx < pGltfData->nodes_count; ++nodeIdx)
    {
        const auto& gltfNode = pGltfData->nodes[nodeIdx];

        // Allocate target node
        auto targetNode = std::make_unique<FauxRender::SceneNode>();
        if (!targetNode)
        {
            return false;
        }

        // Load GLTF node
        bool res = LoadGLTFNode(&internals, pGltfData, &gltfNode, targetNode.get());
        if (!res)
        {
            return false;
        }

        // Add node to graph
        pTargetGraph->Nodes.push_back(std::move(targetNode));
    }

    // -------------------------------------------------------------------------
    // Load meshes
    // -------------------------------------------------------------------------
    {
        GREX_LOG_INFO("  Loading " << internals.MeshMap.size() << " unique meshes");

        for (auto iter : internals.MeshMap)
        {
            auto pGltfMesh   = iter.first;
            auto pTargetMesh = iter.second;

            bool res = LoadGLTFMesh(&internals, loadOptions, pGltfData, pGltfMesh, pTargetMesh);
            if (!res)
            {
                return false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Load geometry data from buffers
    // -------------------------------------------------------------------------
    {
        // Load GLTF buffers from file.
        // These buffers will be destroyed when cgltf_free() is called.
        //
        cgres = cgltf_load_buffers(
            &gltfOptions,
            pGltfData,
            internals.gltfPath.string().c_str());
        if (cgres != cgltf_result_success)
        {
            cgltf_free(pGltfData);
            return false;
        }

        bool res = LoadGLTFGeometryData(&internals, pGltfData);
        if (!res)
        {
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Load materials and associated textures
    // -------------------------------------------------------------------------
    {
        GREX_LOG_INFO("  Loading " << internals.MaterialMap.size() << " unique materials");

        for (auto iter : internals.MaterialMap)
        {
            auto pGltfMaterial   = iter.first;
            auto pTargetMaterial = iter.second;

            bool res = LoadGLTFMaterial(&internals, pGltfData, pGltfMaterial, pTargetMaterial);
            if (!res)
            {
                return false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Load scenes
    // -------------------------------------------------------------------------
    for (size_t sceneIterIdx = 0; sceneIterIdx < pGltfData->scenes_count; ++sceneIterIdx)
    {
        const auto& gltfScene = pGltfData->scenes[sceneIterIdx];

        // Allocate target scene
        auto targetScene = std::make_unique<FauxRender::Scene>();
        if (!targetScene)
        {
            return false;
        }

        // Load GLTF scene
        bool res = LoadGLTFScene(&internals, pGltfData, gltfScene, targetScene.get());
        if (!res)
        {
            return false;
        }

        // Add scene to graph
        pTargetGraph->Scenes.push_back(std::move(targetScene));
    }

    // Free GLTF data
    cgltf_free(pGltfData);

    GREX_LOG_INFO("  Successfully loaded GLTF: " << path);

    return true;
}

} // namespace FauxRender
