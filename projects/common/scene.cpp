#include "scene.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <map>

static GREXFormat ToGREXFormat(const cgltf_accessor* pAccessor)
{
    if (IsNull(pAccessor)) {
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
                case cgltf_component_type_r_16u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32u : return GREX_FORMAT_UNKNOWN;
                case cgltf_component_type_r_32f : return GREX_FORMAT_R32G32B32A32_FLOAT;
            }
        } break;
    }
    // clang-format on

    return GREX_FORMAT_UNKNOWN;
}

bool Scene::LoadGLTF(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        return false;
    }

    GREX_LOG_INFO("Loading GLTF: " << path);

    cgltf_options options = {};
    cgltf_data*   pData   = nullptr;

    // Parse
    cgltf_result cgres = cgltf_parse_file(
        &options,
        path.string().c_str(),
        &pData);
    if (cgres != cgltf_result_success) {
        return false;
    }

    // Load buffers from GLTF file
    cgres = cgltf_load_buffers(
        &options,
        pData,
        path.string().c_str());
    if (cgres != cgltf_result_success) {
        cgltf_free(pData);
        return false;
    }

    // Create buffers
    std::map<std::string, SceneBuffer*> bufferLookUp;
    for (size_t bufferIdx = 0; bufferIdx < pData->buffers_count; ++bufferIdx) {
        const auto& srcBuffer = pData->buffers[bufferIdx];

        SceneBuffer* dstBuffer = nullptr;
        //
        bool res = this->CreateBuffer(
            static_cast<uint32_t>(srcBuffer.size),
            srcBuffer.data,
            false,
            &dstBuffer);
        if (!res) {
            return false;
        }
    }

    // Create meshes
    {
        GREX_LOG_INFO("   Mesh count: " << pData->meshes_count);

        this->Meshes.resize(pData->meshes_count);
        for (size_t meshIdx = 0; meshIdx < pData->meshes_count; ++meshIdx) {
            const auto& srcMesh = pData->meshes[meshIdx];
            auto&       dstMesh = this->Meshes[meshIdx];

            GREX_LOG_INFO("   Mesh " << meshIdx << " : " << (srcMesh.name != nullptr ? srcMesh.name : ""));
            GREX_LOG_INFO("      Batch count: " << srcMesh.primitives_count);

            // Name
            if (!IsNull(srcMesh.name)) {
                dstMesh.Name = srcMesh.name;
            }

            // Batches
            dstMesh.Batches.resize(srcMesh.primitives_count);
            for (size_t primsIdx = 0; primsIdx < srcMesh.primitives_count; ++primsIdx) {
                const auto& srcPrims = srcMesh.primitives[primsIdx];
                auto&       batch    = dstMesh.Batches[primsIdx];

                // Material
                batch.MaterialIndex = UINT32_MAX;
                if (!IsNull(srcPrims.material)) {
                    batch.MaterialIndex = cgltf_material_index(pData, srcPrims.material);
                }

                // Indices
                {
                    const auto& pSrcIndexData = srcPrims.indices;
                    const auto& pSrcView      = pSrcIndexData->buffer_view;
                    const auto& pSrcBuffer    = pSrcView->buffer;
                    auto        bufferIndex   = cgltf_buffer_index(pData, pSrcBuffer);
                    assert((bufferIndex < pData->buffers_count) && "index buffer index exceeds buffer count");

                    batch.IndexBufferView.pBuffer = this->Buffers[bufferIndex].get();
                    batch.IndexBufferView.Offset  = static_cast<uint32_t>(pSrcView->offset);
                    batch.IndexBufferView.Size    = static_cast<uint32_t>(pSrcView->size);
                    batch.IndexBufferView.Format  = ToGREXFormat(pSrcIndexData);
                    batch.IndexBufferView.Count   = static_cast<uint32_t>(pSrcIndexData->count);
                    GREX_LOG_INFO("      Index count: " << pSrcIndexData->count);
                }

                // Vertices
                for (size_t attrIdx = 0; attrIdx < srcPrims.attributes_count; ++attrIdx) {
                    const auto& srcAttr        = srcPrims.attributes[attrIdx];
                    const auto& pSrcVertexData = srcAttr.data;
                    const auto& pSrcView       = pSrcVertexData->buffer_view;
                    const auto& pSrcBuffer     = pSrcView->buffer;
                    auto        format         = ToGREXFormat(pSrcVertexData);
                    auto        bufferIndex    = cgltf_buffer_index(pData, pSrcBuffer);
                    assert((bufferIndex < pData->buffers_count) && "vertex buffer index exceeds buffer count");

                    SceneVertexBufferView* pView = nullptr;
                    switch (srcAttr.type) {
                        default: {
                            assert(false && "unsupported attribute type");
                        } break;

                        case cgltf_attribute_type_position: {
                            pView = &batch.PositionBufferView;
                            assert((format == GREX_FORMAT_R32G32B32_FLOAT) && "invalid position attribute format");
                        } break;

                        case cgltf_attribute_type_normal: {
                            pView = &batch.NormalBufferView;
                            assert((format == GREX_FORMAT_R32G32B32_FLOAT) && "invalid normal attribute format");
                        } break;

                        case cgltf_attribute_type_tangent: {
                            pView = &batch.TangentBufferView;
                            assert((format == GREX_FORMAT_R32G32B32A32_FLOAT) && "invalid tangent attribute format");
                        } break;

                        case cgltf_attribute_type_texcoord: {
                            pView = &batch.TexCoordBufferView;
                            assert((format == GREX_FORMAT_R32G32_FLOAT) && "invalid tex coord attribute format");
                        } break;

                        case cgltf_attribute_type_color: {
                            pView = &batch.VertexColorBufferView;
                            assert((format == GREX_FORMAT_R32G32B32_FLOAT) && "invalid vertex color attribute format");
                        } break;
                    }

                    if (!IsNull(pView)) {
                        pView->pBuffer = this->Buffers[bufferIndex].get();
                        pView->Offset  = static_cast<uint32_t>(pSrcView->offset);
                        pView->Size    = static_cast<uint32_t>(pSrcView->size);
                        pView->Stride  = static_cast<uint32_t>(pSrcVertexData->stride);
                        pView->Format  = format;
                    }
                }
            }
        }
    }

    // Create nodes
    {
        GREX_LOG_INFO("   Node count: " << pData->nodes_count);

        for (size_t nodeIdx = 0; nodeIdx < pData->nodes_count; ++nodeIdx) {
            const auto& srcNode = pData->nodes[nodeIdx];

            // Skip for now if there's no mesh
            if (IsNull(srcNode.mesh)) {
                continue;
            }

            auto meshIndex = cgltf_mesh_index(pData, srcNode.mesh);
            assert((meshIndex < pData->meshes_count) && "mesh index exceeds mesh count");

            SceneNode dstNode = {};
            dstNode.MeshIndex = static_cast<uint32_t>(meshIndex);

            dstNode.Translate = vec3(0);
            if (srcNode.has_translation) {
                dstNode.Translate = vec3(srcNode.translation[0], srcNode.translation[1], srcNode.translation[2]);
            }

            dstNode.Rotation = quat(0, 0, 0, 1);
            if (srcNode.has_rotation) {
                dstNode.Rotation = quat(srcNode.rotation[0], srcNode.rotation[1], srcNode.rotation[1], srcNode.rotation[3]);
            }

            dstNode.Scale = vec3(1);
            if (srcNode.has_scale) {
                dstNode.Scale = vec3(srcNode.scale[0], srcNode.scale[1], srcNode.scale[2]);
            }

            this->Nodes.push_back(dstNode);
        }
    }

    cgltf_free(pData);

    GREX_LOG_INFO("   Successfully loaded GLTF: " << path);

    return true;
}