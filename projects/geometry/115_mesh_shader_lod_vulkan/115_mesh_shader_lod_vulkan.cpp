#include "window.h"
#include "camera.h"

#include "vk_renderer.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include "meshoptimizer.h"

#include <cinttypes>

#define CHECK_CALL(FN)                               \
    {                                                \
        VkResult vkres = FN;                         \
        if (vkres != VK_SUCCESS)                     \
        {                                            \
            std::stringstream ss;                    \
            ss << "\n";                              \
            ss << "*** FUNCTION CALL FAILED *** \n"; \
            ss << "FUNCTION: " << #FN << "\n";       \
            ss << "\n";                              \
            GREX_LOG_ERROR(ss.str().c_str());        \
            assert(false);                           \
        }                                            \
    }

// =============================================================================
// Scene Stuff
// =============================================================================
struct SceneProperties
{
    mat4  CameraVP;
    uint  InstanceCount;
    uint  MeshletCount;
    uint  __pad0[2];
    uvec4 Meshlet_LOD_Offsets[5];
    uvec4 Meshlet_LOD_Counts[5];
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = false;

void CreatePipelineLayout(
    VulkanRenderer*        pRenderer,
    VkPipelineLayout*      pPipelineLayout,
    VkDescriptorSetLayout* pDescriptorSetLayout);
void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvAS,
    const std::vector<uint32_t>& spirvMS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleAS,
    VkShaderModule*              pModuleMS,
    VkShaderModule*              pModuleFS);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    VulkanFeatures features       = {};
    features.EnableMeshShader     = true;
    features.EnablePushDescriptor = true;
    if (!InitVulkan(renderer.get(), gEnableDebug, features))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    //
    // Make sure the shaders compile before we do anything.
    //
    // *************************************************************************
    std::vector<uint32_t> spirvAS;
    std::vector<uint32_t> spirvMS;
    std::vector<uint32_t> spirvFS;
    {
        auto source = LoadString("projects/115_mesh_shader_lod/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        auto        hr = CompileHLSL(source, "asmain", "as_6_5", &spirvAS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (AS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "msmain", "ms_6_5", &spirvMS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (MS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "psmain", "ps_6_5", &spirvFS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (FS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Load mesh LODs
    // *************************************************************************
    std::vector<TriMesh> meshLODs;
    {
        // LOD 0
        {
            TriMesh mesh = {};
            bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/horse_statue_01_1k.obj").string(), &mesh);
            if (!res)
            {
                assert(false && "failed to load model LOD 0");
                return EXIT_FAILURE;
            }
            meshLODs.push_back(mesh);
        }

        // LOD 1
        {
            TriMesh mesh = {};
            bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/horse_statue_01_1k_LOD_1.obj").string(), &mesh);
            if (!res)
            {
                assert(false && "failed to load model LOD 1");
                return EXIT_FAILURE;
            }
            meshLODs.push_back(mesh);
        }

        // LOD 2
        {
            TriMesh mesh = {};
            bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/horse_statue_01_1k_LOD_2.obj").string(), &mesh);
            if (!res)
            {
                assert(false && "failed to load model LOD 2");
                return EXIT_FAILURE;
            }
            meshLODs.push_back(mesh);
        }

        // LOD 3
        {
            TriMesh mesh = {};
            bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/horse_statue_01_1k_LOD_3.obj").string(), &mesh);
            if (!res)
            {
                assert(false && "failed to load model LOD 3");
                return EXIT_FAILURE;
            }
            meshLODs.push_back(mesh);
        }

        // LOD 4
        {
            TriMesh mesh = {};
            bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/horse_statue_01_1k_LOD_4.obj").string(), &mesh);
            if (!res)
            {
                assert(false && "failed to load model LOD 4");
                return EXIT_FAILURE;
            }
            meshLODs.push_back(mesh);
        }
    }

    // *************************************************************************
    // Make them meshlets!
    // *************************************************************************
    TriMesh::Aabb                meshBounds = meshLODs[0].GetBounds();
    std::vector<vec3>            combinedMeshPositions;
    std::vector<meshopt_Meshlet> combinedMeshlets;
    std::vector<uint32_t>        combinedMeshletVertices;
    std::vector<uint8_t>         combinedMeshletTriangles;
    std::vector<uint32_t>        meshlet_LOD_Offsets;
    std::vector<uint32_t>        meshlet_LOD_Counts;
    uint32_t                     LOD_0_vertexCount   = 0;
    uint32_t                     LOD_0_triangleCount = 0;

    for (size_t lodIdx = 0; lodIdx < meshLODs.size(); ++lodIdx)
    {
        const auto& mesh = meshLODs[lodIdx];

        const size_t kMaxVertices  = 64;
        const size_t kMaxTriangles = 124;
        const float  kConeWeight   = 0.0f;

        std::vector<meshopt_Meshlet> meshlets;
        std::vector<uint32_t>        meshletVertices;
        std::vector<uint8_t>         meshletTriangles;

        const size_t maxMeshlets = meshopt_buildMeshletsBound(mesh.GetNumIndices(), kMaxVertices, kMaxTriangles);

        meshlets.resize(maxMeshlets);
        meshletVertices.resize(maxMeshlets * kMaxVertices);
        meshletTriangles.resize(maxMeshlets * kMaxTriangles * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshlets.data(),
            meshletVertices.data(),
            meshletTriangles.data(),
            reinterpret_cast<const uint32_t*>(mesh.GetTriangles().data()),
            mesh.GetNumIndices(),
            reinterpret_cast<const float*>(mesh.GetPositions().data()),
            mesh.GetNumVertices(),
            sizeof(vec3),
            kMaxVertices,
            kMaxTriangles,
            kConeWeight);

        auto& last = meshlets[meshletCount - 1];
        meshletVertices.resize(last.vertex_offset + last.vertex_count);
        meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshletCount);

        // Meshlet LOD offset and count
        meshlet_LOD_Offsets.push_back(static_cast<uint32_t>(combinedMeshlets.size()));
        meshlet_LOD_Counts.push_back(static_cast<uint32_t>(meshlets.size()));

        // Current offsets
        const uint32_t vertexOffset          = static_cast<uint32_t>(combinedMeshPositions.size());
        const uint32_t meshletVertexOffset   = static_cast<uint32_t>(combinedMeshletVertices.size());
        const uint32_t meshletTriangleOffset = static_cast<uint32_t>(combinedMeshletTriangles.size());

        // Copy to combined
        std::copy(mesh.GetPositions().begin(), mesh.GetPositions().end(), std::back_inserter(combinedMeshPositions));

        for (auto meshlet : meshlets)
        {
            meshlet.vertex_offset += meshletVertexOffset;
            meshlet.triangle_offset += meshletTriangleOffset;
            combinedMeshlets.push_back(meshlet);

            if (lodIdx == 0)
            {
                LOD_0_vertexCount += meshlet.vertex_count;
                LOD_0_triangleCount += meshlet.triangle_count;
            }
        }

        for (auto vertex : meshletVertices)
        {
            vertex += vertexOffset;
            combinedMeshletVertices.push_back(vertex);
        }

        std::copy(meshletTriangles.begin(), meshletTriangles.end(), std::back_inserter(combinedMeshletTriangles));
    }

    // Meshlet bounds (we're using bounding spheres)
    std::vector<vec4> meshletBounds;
    for (auto& meshlet : combinedMeshlets)
    {
        auto bounds = meshopt_computeMeshletBounds(
            &combinedMeshletVertices[meshlet.vertex_offset],
            &combinedMeshletTriangles[meshlet.triangle_offset],
            meshlet.triangle_count,
            reinterpret_cast<const float*>(combinedMeshPositions.data()),
            combinedMeshPositions.size(),
            sizeof(vec3));
        meshletBounds.push_back(vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius));
    }

    // Get some counts to use later
    uint64_t combinedMeshletVertexCount   = 0;
    uint64_t combinedMeshletTriangleCount = 0;
    for (auto& m : combinedMeshlets)
    {
        combinedMeshletVertexCount += m.vertex_count;
        combinedMeshletTriangleCount += m.triangle_count;
    }

    // Repack triangles from 3 consecutive byes to 4-byte uint32_t to
    // make it easier to unpack on the GPU.
    //
    std::vector<uint32_t> meshletTrianglesU32;
    for (auto& m : combinedMeshlets)
    {
        // Save triangle offset for current meshlet
        uint32_t triangleOffset = static_cast<uint32_t>(meshletTrianglesU32.size());

        // Repack to uint32_t
        for (uint32_t i = 0; i < m.triangle_count; ++i)
        {
            uint32_t i0 = 3 * i + 0 + m.triangle_offset;
            uint32_t i1 = 3 * i + 1 + m.triangle_offset;
            uint32_t i2 = 3 * i + 2 + m.triangle_offset;

            uint8_t  vIdx0  = combinedMeshletTriangles[i0];
            uint8_t  vIdx1  = combinedMeshletTriangles[i1];
            uint8_t  vIdx2  = combinedMeshletTriangles[i2];
            uint32_t packed = ((static_cast<uint32_t>(vIdx0) & 0xFF) << 0) |
                              ((static_cast<uint32_t>(vIdx1) & 0xFF) << 8) |
                              ((static_cast<uint32_t>(vIdx2) & 0xFF) << 16);
            meshletTrianglesU32.push_back(packed);
        }

        // Update triangle offset for current meshlet
        m.triangle_offset = triangleOffset;
    }

    VulkanBuffer positionBuffer;
    VulkanBuffer meshletBuffer;
    VulkanBuffer meshletVerticesBuffer;
    VulkanBuffer meshletTrianglesBuffer;
    VulkanBuffer meshletBoundsBuffer;
    {
        VkBufferUsageFlags usageFlags  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaMemoryUsage     memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshPositions), DataPtr(combinedMeshPositions), usageFlags, memoryUsage, 0, &positionBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshlets), DataPtr(combinedMeshlets), usageFlags, memoryUsage, 0, &meshletBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshletVertices), DataPtr(combinedMeshletVertices), usageFlags, memoryUsage, 0, &meshletVerticesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletTrianglesU32), DataPtr(meshletTrianglesU32), usageFlags, memoryUsage, 0, &meshletTrianglesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletBounds), DataPtr(meshletBounds), usageFlags, memoryUsage, 0, &meshletBoundsBuffer));
    }

    // *************************************************************************
    // Pipeline layout
    //
    // This is used for pipeline creation
    //
    // *************************************************************************
    VkPipelineLayout      pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    CreatePipelineLayout(renderer.get(), &pipelineLayout, &descriptorSetLayout);

    // *************************************************************************
    // Shader module
    // *************************************************************************
    VkShaderModule moduleAS = VK_NULL_HANDLE;
    VkShaderModule moduleMS = VK_NULL_HANDLE;
    VkShaderModule moduleFS = VK_NULL_HANDLE;
    CreateShaderModules(
        renderer.get(),
        spirvAS,
        spirvMS,
        spirvFS,
        &moduleAS,
        &moduleMS,
        &moduleFS);

    // *************************************************************************
    // Create the pipeline
    // *************************************************************************
    VkPipeline pipeline = VK_NULL_HANDLE;
    CreateMeshShaderPipeline(
        renderer.get(),
        pipelineLayout,
        moduleAS,
        moduleMS,
        moduleFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pipeline,
        VK_CULL_MODE_NONE);

    // *************************************************************************
    // Get descriptor buffer properties
    // *************************************************************************
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    {
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &descriptorBufferProperties;
        vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, GREX_BASE_FILE_NAME());
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight()))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain image views, depth buffers/views
    // *************************************************************************
    std::vector<VkImage>     swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkImageView> swapchainDepthViews;
    {
        CHECK_CALL(GetSwapchainImages(renderer.get(), swapchainImages));

        for (auto& image : swapchainImages)
        {
            // Create swap chain images
            VkImageViewCreateInfo createInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image                           = image;
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = GREX_DEFAULT_RTV_FORMAT;
            createInfo.components                      = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            CHECK_CALL(vkCreateImageView(renderer->Device, &createInfo, nullptr, &imageView));

            swapchainImageViews.push_back(imageView);
        }

        size_t imageCount = swapchainImages.size();

        std::vector<VulkanImage> depthImages;
        depthImages.resize(swapchainImages.size());

        for (int depthIndex = 0; depthIndex < imageCount; depthIndex++)
        {
            // Create depth images
            CHECK_CALL(CreateDSV(renderer.get(), window->GetWidth(), window->GetHeight(), &depthImages[depthIndex]));

            VkImageViewCreateInfo createInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image                           = depthImages[depthIndex].Image;
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = GREX_DEFAULT_DSV_FORMAT;
            createInfo.components                      = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            VkImageView depthView = VK_NULL_HANDLE;
            CHECK_CALL(vkCreateImageView(renderer->Device, &createInfo, nullptr, &depthView));

            swapchainDepthViews.push_back(depthView);
        }
    }

    // *************************************************************************
    // Render pass to draw ImGui
    // *************************************************************************
    std::vector<VulkanAttachmentInfo> colorAttachmentInfos = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, renderer->SwapchainImageUsage}
    };

    VulkanRenderPass renderPass = {};
    CHECK_CALL(CreateRenderPass(renderer.get(), colorAttachmentInfos, {}, gWindowWidth, gWindowHeight, &renderPass));

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForVulkan(renderer.get(), renderPass.RenderPass))
    {
        assert(false && "GrexWindow::InitImGuiForD3D12 failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command buffer
    // *************************************************************************
    CommandObjects cmdBuf = {};
    {
        CHECK_CALL(CreateCommandBuffer(renderer.get(), 0, &cmdBuf));
    }

    // *************************************************************************
    // Pipeline statistics
    // *************************************************************************
    VkQueryPool queryPool = VK_NULL_HANDLE;
    if (renderer->HasMeshShaderQueries)
    {
        VkQueryPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        createInfo.flags                 = 0;
        createInfo.queryType             = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        createInfo.queryCount            = 1;
        createInfo.pipelineStatistics    = 0;
        //
        // NOTE: Disabling this for now, for some reason having
        //       VK_QUERY_PIPELINE_STATISTIC_MESH_SHADER_INVOCATIONS_BIT_EXT
        //       in the pipeline statistic causes a massive performance drop
        //       on NVIDIA.
        /*
        VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
        VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT |
        VK_QUERY_PIPELINE_STATISTIC_TASK_SHADER_INVOCATIONS_BIT_EXT |
        VK_QUERY_PIPELINE_STATISTIC_MESH_SHADER_INVOCATIONS_BIT_EXT;
        */

        CHECK_CALL(vkCreateQueryPool(renderer.get()->Device, &createInfo, nullptr, &queryPool));
    }
    bool hasPiplineStats = false;

    // *************************************************************************
    // Scene and constant buffer
    // *************************************************************************
    SceneProperties scene = {};

    VulkanBuffer sceneBuffer;
    {
        size_t size = Align<size_t>(sizeof(SceneProperties), 256);
        CHECK_CALL(CreateBuffer(renderer.get(), sizeof(SceneProperties), nullptr, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0, &sceneBuffer));
    }

    // *************************************************************************
    // Instances
    // *************************************************************************
    const uint32_t    kNumInstanceCols = 1;
    const uint32_t    kNumInstanceRows = 5;
    std::vector<mat4> instances(kNumInstanceCols * kNumInstanceRows);

    VulkanBuffer instancesBuffer;
    {
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(instances), DataPtr(instances), usageFlags, 0, &instancesBuffer));
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    VkClearValue clearValues[2];
    clearValues[0].color        = {0.23f, 0.23f, 0.31f, 0};
    clearValues[1].depthStencil = {1.0f, 0};

    while (window->PollEvents())
    {
        // Should match up with what was specified in the query pool's create info
        std::vector<uint64_t> pipelineStatistics(13);
        std::memset(DataPtr(pipelineStatistics), 0, SizeInBytes(pipelineStatistics));

        if ((queryPool != VK_NULL_HANDLE) && hasPiplineStats)
        {
            VkDeviceSize stride = static_cast<VkDeviceSize>(SizeInBytes(pipelineStatistics));

            //
            // NOTE: For some reason pipeline statistics returns information for tessellation
            //       shaders even though there's no tessellation shader present in the pipeline.
            //
            vkGetQueryPoolResults(
                renderer.get()->Device,                             // device
                queryPool,                                          // queryPool
                0,                                                  // firstQuery
                1,                                                  // queryCount
                SizeInBytes(pipelineStatistics),                    // dataSize
                DataPtr(pipelineStatistics),                        // pData
                stride,                                             // stride
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT); // flags
        }

        // ---------------------------------------------------------------------
        window->ImGuiNewFrameVulkan();

        if (ImGui::Begin("Params"))
        {
            auto instanceCount              = instances.size();
            auto totalMeshletCount          = meshlet_LOD_Counts[0] * instanceCount;
            auto totalMeshletVertexCount    = LOD_0_vertexCount * instanceCount;
            auto totalMeshletPrimitiveCount = LOD_0_triangleCount * instanceCount;

            ImGui::Columns(2);
            // clang-format off
            ImGui::Text("LOD 0 Meshlet Count");               ImGui::NextColumn(); ImGui::Text("%d", meshlet_LOD_Counts[0]); ImGui::NextColumn();
            ImGui::Text("LOD 0 Meshlet Vertex Count");        ImGui::NextColumn(); ImGui::Text("%d", LOD_0_vertexCount); ImGui::NextColumn();
            ImGui::Text("LOD 0 Meshlet Primitive Count");     ImGui::NextColumn(); ImGui::Text("%d", LOD_0_triangleCount); ImGui::NextColumn();
            ImGui::Text("Instance Count");                    ImGui::NextColumn(); ImGui::Text("%d", instanceCount); ImGui::NextColumn();                
            ImGui::Text("Instanced Meshlet Count");           ImGui::NextColumn(); ImGui::Text("%d", totalMeshletCount); ImGui::NextColumn();                
            ImGui::Text("Instanced Meshlet Vertex Count");    ImGui::NextColumn(); ImGui::Text("%d", totalMeshletVertexCount); ImGui::NextColumn();                
            ImGui::Text("Instanced Meshlet Primitive Count"); ImGui::NextColumn(); ImGui::Text("%d", totalMeshletPrimitiveCount); ImGui::NextColumn();                 
            ImGui::Columns(2);
            // clang-format on
            ImGui::Columns(1);

            ImGui::Separator();

            ImGui::Columns(2);
            // clang-format off
            ImGui::Text("Input Assembly Vertices"     ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 0]); ImGui::NextColumn();
            ImGui::Text("Input Assembly Primitives"   ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 1]); ImGui::NextColumn();
            ImGui::Text("Vertex Shader Invocations"   ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 2]); ImGui::NextColumn();
            ImGui::Text("Geometry Shader Invocations" ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 3]); ImGui::NextColumn();
            ImGui::Text("Geometry Shader Primitives"  ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 4]); ImGui::NextColumn();
            ImGui::Text("Clipping Invocations"        ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 4]); ImGui::NextColumn();
            ImGui::Text("Clipping Primitives"         ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 5]); ImGui::NextColumn();
            ImGui::Text("Fragment Shader Invocations" ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 6]); ImGui::NextColumn();
            ImGui::Text("Tess Ctrl Shader Patches"    ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 7]); ImGui::NextColumn();
            ImGui::Text("Tess Eval Shader Invocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[ 9]); ImGui::NextColumn();
            ImGui::Text("Compute Shader Invocations"  ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[10]); ImGui::NextColumn();
            ImGui::Text("Task Shader Invocations"     ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[11]); ImGui::NextColumn();
            ImGui::Text("Mesh Shader Invocations"     ); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics[12]); ImGui::NextColumn();

            // clang-format on
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        // Update instance transforms
        float farDist = 1000.0f;
        {
            float maxSpan       = std::max<float>(meshBounds.Width(), meshBounds.Depth());
            float instanceSpanX = 4.0f * maxSpan;
            float instanceSpanZ = 4.5f * maxSpan;
            float totalSpanX    = kNumInstanceCols * instanceSpanX;
            float totalSpanZ    = kNumInstanceRows * instanceSpanZ;

            float t = static_cast<float>(glfwGetTime());

            // 0
            {
                vec3 P       = vec3(0, 0, -static_cast<float>(0 * instanceSpanZ));
                instances[0] = glm::translate(P) * glm::rotate(t, vec3(0, 1, 0));
            }

            // 1
            {
                vec3 P       = vec3(0, 0, -static_cast<float>(0.75f * instanceSpanZ));
                instances[1] = glm::translate(P) * glm::rotate(t, vec3(0, 1, 0));
            }

            // 2
            {
                vec3 P       = vec3(0, 0, -static_cast<float>(2.5 * instanceSpanZ));
                instances[2] = glm::translate(P) * glm::rotate(t, vec3(0, 1, 0));
            }

            // 3
            {
                vec3 P       = vec3(0, 0, -static_cast<float>(8 * instanceSpanZ));
                instances[3] = glm::translate(P) * glm::rotate(t, vec3(0, 1, 0));
            }

            // 4
            {
                vec3 P       = vec3(0, 0, -static_cast<float>(40 * instanceSpanZ));
                instances[4] = glm::translate(P) * glm::rotate(t, vec3(0, 1, 0));
            }
        }

        // ---------------------------------------------------------------------

        // Update scene
        {
            vec3 eyePosition = vec3(0.3f, 0.125f, 0.525f);
            vec3 target      = vec3(0, 0.1f, -0.425f);

            PerspCamera camera = PerspCamera(45.0f, window->GetAspectRatio(), 0.1f, farDist);
            camera.LookAt(eyePosition, target);

            Camera::FrustumPlane frLeft, frRight, frTop, frBottom, frNear, frFar;
            camera.GetFrustumPlanes(&frLeft, &frRight, &frTop, &frBottom, &frNear, &frFar);

            scene.CameraVP                 = camera.GetViewProjectionMatrix();
            scene.InstanceCount            = static_cast<uint32_t>(instances.size());
            scene.MeshletCount             = meshlet_LOD_Counts[0];
            scene.Meshlet_LOD_Offsets[0].x = meshlet_LOD_Offsets[0];
            scene.Meshlet_LOD_Offsets[1].x = meshlet_LOD_Offsets[1];
            scene.Meshlet_LOD_Offsets[2].x = meshlet_LOD_Offsets[2];
            scene.Meshlet_LOD_Offsets[3].x = meshlet_LOD_Offsets[3];
            scene.Meshlet_LOD_Offsets[4].x = meshlet_LOD_Offsets[4];
            scene.Meshlet_LOD_Counts[0].x  = meshlet_LOD_Counts[0];
            scene.Meshlet_LOD_Counts[1].x  = meshlet_LOD_Counts[1];
            scene.Meshlet_LOD_Counts[2].x  = meshlet_LOD_Counts[2];
            scene.Meshlet_LOD_Counts[3].x  = meshlet_LOD_Counts[3];
            scene.Meshlet_LOD_Counts[4].x  = meshlet_LOD_Counts[4];

            void* pDst = nullptr;
            CHECK_CALL(vmaMapMemory(renderer.get()->Allocator, sceneBuffer.Allocation, reinterpret_cast<void**>(&pDst)));
            memcpy(pDst, &scene, sizeof(SceneProperties));
            vmaUnmapMemory(renderer.get()->Allocator, sceneBuffer.Allocation);
        }

        // ---------------------------------------------------------------------

        // Copy instances transforms to instances buffer
        {
            void* pDst = nullptr;
            CHECK_CALL(vmaMapMemory(renderer.get()->Allocator, instancesBuffer.Allocation, reinterpret_cast<void**>(&pDst)));
            memcpy(pDst, instances.data(), SizeInBytes(instances));
            vmaUnmapMemory(renderer.get()->Allocator, instancesBuffer.Allocation);
        }

        // ---------------------------------------------------------------------

        uint32_t swapchainImageIndex = 0;
        if (AcquireNextImage(renderer.get(), &swapchainImageIndex))
        {
            assert(false && "AcquireNextImage failed");
            break;
        }

        VkCommandBufferBeginInfo vkbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));
        {
            // Reset query pool - this needs to happen outside of render pass
            if (queryPool != VK_NULL_HANDLE) {
                vkCmdResetQueryPool(cmdBuf.CommandBuffer, queryPool, 0, 1);
            }

            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                swapchainImages[swapchainImageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_PRESENT,
                RESOURCE_STATE_RENDER_TARGET);

            VkRenderingAttachmentInfo colorAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView                 = swapchainImageViews[swapchainImageIndex];
            colorAttachment.imageLayout               = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue                = clearValues[0];

            VkRenderingAttachmentInfo depthAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView                 = swapchainDepthViews[swapchainImageIndex];
            depthAttachment.imageLayout               = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue                = clearValues[1];

            VkRenderingInfo vkri          = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            vkri.layerCount               = 1;
            vkri.colorAttachmentCount     = 1;
            vkri.pColorAttachments        = &colorAttachment;
            vkri.pDepthAttachment         = &depthAttachment;
            vkri.renderArea.extent.width  = gWindowWidth;
            vkri.renderArea.extent.height = gWindowHeight;

            vkCmdBeginRendering(cmdBuf.CommandBuffer, &vkri);

            VkViewport viewport = {0, static_cast<float>(gWindowHeight), static_cast<float>(gWindowWidth), -static_cast<float>(gWindowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(cmdBuf.CommandBuffer, 0, 1, &viewport);

            VkRect2D scissor = {0, 0, gWindowWidth, gWindowHeight};
            vkCmdSetScissor(cmdBuf.CommandBuffer, 0, 1, &scissor);

            vkCmdBindPipeline(cmdBuf.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &sceneBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &positionBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletBoundsBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletVerticesBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &meshletTrianglesBuffer);
            PushGraphicsDescriptor(cmdBuf.CommandBuffer, pipelineLayout, 0, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instancesBuffer);

            // vkCmdDrawMeshTasksEXT with pipeline statistics
            {
                if (queryPool != VK_NULL_HANDLE)
                {
                    vkCmdBeginQuery(cmdBuf.CommandBuffer, queryPool, 0, 0);
                }

                // Task (amplification) shader uses 32 for thread group size
                uint32_t meshletCount      = static_cast<uint32_t>(meshlet_LOD_Counts[0]);
                uint32_t instanceCount     = static_cast<uint32_t>(instances.size());
                uint32_t threadGroupCountX = ((meshletCount * instanceCount) / 32) + 1;
                fn_vkCmdDrawMeshTasksEXT(cmdBuf.CommandBuffer, threadGroupCountX, 1, 1);

                if (queryPool != VK_NULL_HANDLE)
                {
                    vkCmdEndQuery(cmdBuf.CommandBuffer, queryPool, 0);
                }
            }

            vkCmdEndRendering(cmdBuf.CommandBuffer);

            // ImGui
            {
                VkRenderPassAttachmentBeginInfo attachmentBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
                attachmentBeginInfo.pNext                           = 0;
                attachmentBeginInfo.attachmentCount                 = 1;
                attachmentBeginInfo.pAttachments                    = &swapchainImageViews[swapchainImageIndex];

                VkRenderPassBeginInfo beginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                beginInfo.pNext                 = &attachmentBeginInfo;
                beginInfo.renderPass            = renderPass.RenderPass;
                beginInfo.framebuffer           = renderPass.Framebuffer;
                beginInfo.renderArea            = scissor;

                vkCmdBeginRenderPass(cmdBuf.CommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Draw ImGui
                window->ImGuiRenderDrawData(renderer.get(), cmdBuf.CommandBuffer);

                vkCmdEndRenderPass(cmdBuf.CommandBuffer);
            }

            CmdTransitionImageLayout(
                cmdBuf.CommandBuffer,
                swapchainImages[swapchainImageIndex],
                GREX_ALL_SUBRESOURCES,
                VK_IMAGE_ASPECT_COLOR_BIT,
                RESOURCE_STATE_RENDER_TARGET,
                RESOURCE_STATE_PRESENT);
        }

        CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

        // Execute command buffer
        CHECK_CALL(ExecuteCommandBuffer(renderer.get(), &cmdBuf));

        // Wait for the GPU to finish the work
        if (!WaitForGpu(renderer.get()))
        {
            assert(false && "WaitForGpu failed");
        }

        // Command list execution is done we can read the pipeline stats
        hasPiplineStats = true;

        if (!SwapchainPresent(renderer.get(), swapchainImageIndex))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreatePipelineLayout(
    VulkanRenderer*        pRenderer,
    VkPipelineLayout*      pPipelineLayout,
    VkDescriptorSetLayout* pDescriptorSetLayout)
{
    VkPushConstantRange push_constant = {};
    push_constant.offset              = 0;
    push_constant.size                = sizeof(mat4) + sizeof(uint32_t);
    push_constant.stageFlags          = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {};

    // ConstantBuffer<SceneProperties> Scene : register(b0);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 0;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<Vertex> Vertices : register(t1);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 1;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<Meshlet> Meshlets : register(t2);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 2;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<float4> MeshletBounds : register(t3);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 3;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<uint> VertexIndices : register(t4);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 4;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // StructuredBuffer<Instance> TriangleIndices : register(t5);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 5;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT;

        bindings.push_back(binding);
    }

    // ByteAddressBuffer Instances : register(t6);
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding                      = 6;
        binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount              = 1;
        binding.stageFlags                   = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

        bindings.push_back(binding);
    }

    // Create descriptor set
    {
        VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        createInfo.bindingCount                    = CountU32(bindings);
        createInfo.pBindings                       = DataPtr(bindings);

        CHECK_CALL(vkCreateDescriptorSetLayout(pRenderer->Device, &createInfo, nullptr, pDescriptorSetLayout));
    }

    // Create pipeline layout
    {
        VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        createInfo.setLayoutCount             = 1;
        createInfo.pSetLayouts                = pDescriptorSetLayout;

        CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, pPipelineLayout));
    }
}

void CreateShaderModules(
    VulkanRenderer*              pRenderer,
    const std::vector<uint32_t>& spirvAS,
    const std::vector<uint32_t>& spirvMS,
    const std::vector<uint32_t>& spirvFS,
    VkShaderModule*              pModuleAS,
    VkShaderModule*              pModuleMS,
    VkShaderModule*              pModuleFS)
{
    // Amplification Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvAS);
        createInfo.pCode                    = DataPtr(spirvAS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleAS));
    }

    // Mesh Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvMS);
        createInfo.pCode                    = DataPtr(spirvMS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleMS));
    }

    // Fragment Shader
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize                 = SizeInBytes(spirvFS);
        createInfo.pCode                    = DataPtr(spirvFS);

        CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleFS));
    }
}
