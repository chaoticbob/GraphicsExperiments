#include "window.h"
#include "camera.h"
#include "tri_mesh.h"

#include "dx_renderer.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include "meshoptimizer.h"

#include <cinttypes>

using float4x4 = glm::mat4;

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr))                              \
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
using float3 = glm::vec3;
using float4 = glm::vec4;
using uint4  = glm::uvec4;

enum
{
    FRUSTUM_PLANE_LEFT   = 0,
    FRUSTUM_PLANE_RIGHT  = 1,
    FRUSTUM_PLANE_TOP    = 2,
    FRUSTUM_PLANE_BOTTOM = 3,
    FRUSTUM_PLANE_NEAR   = 4,
    FRUSTUM_PLANE_FAR    = 5,
};

struct FrustumPlane
{
    float3 Normal;
    float  __pad0;
    float3 Position;
    float  __pad1;
};

struct FrustumCone
{
    float3 Tip;
    float  Height;
    float3 Direction;
    float  Angle;
};

struct FrustumData
{
    FrustumPlane Planes[6];
    float4       Sphere;
    FrustumCone  Cone;
};

struct SceneProperties
{
    float4x4    CameraVP;
    FrustumData Frustum;
    uint        InstanceCount;
    uint        MeshletCount;
    uint        VisibilityFunc;
    uint        __pad0;
    uint4       Meshlet_LOD_Offsets[5];
    uint4       Meshlet_LOD_Counts[5];
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = false;

static bool gFitConeToFarClip = false;

enum VisibilityFunc
{
    VISIBILITY_FUNC_NONE                = 0,
    VISIBILITY_FUNC_PLANES              = 1,
    VISIBILITY_FUNC_SPHERE              = 2,
    VISIBILITY_FUNC_CONE                = 3,
    VISIBILITY_FUNC_CONE_AND_NEAR_PLANE = 4,
};

static std::vector<std::string> gVisibilityFuncNames = {
    "None",
    "Frustum Planes",
    "Frustum Sphere",
    "Frustum Cone",
    "Frustum Cone and Near Plane",
};

static int gVisibilityFunc = VISIBILITY_FUNC_PLANES;

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppVertexBuffer,
    ID3D12Resource** ppVertexColorBuffer);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    // *************************************************************************
    // Renderer
    // *************************************************************************
    std::unique_ptr<DxRenderer> renderer = std::make_unique<DxRenderer>();
    if (!InitDx(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    CHECK_CALL(renderer->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)));

    bool isMeshShadingSupported = (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
    if (!isMeshShadingSupported)
    {
        assert(false && "Required mesh shading tier not supported");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<char> dxilAS;
    std::vector<char> dxilMS;
    std::vector<char> dxilPS;
    {
        auto source = LoadString("projects/115_mesh_shader_lod/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(source, "asmain", "as_6_5", &dxilAS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (AS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "msmain", "ms_6_5", &dxilMS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (MS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "psmain", "ps_6_5", &dxilPS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Load mesh LODs
    // *************************************************************************
    std::vector<TriMesh> meshLODs;
    {
        TriMesh::Options options = {};

        // LOD 0
        {
            TriMesh mesh = {};
            bool    res  = TriMesh::LoadOBJ(GetAssetPath("models/horse_statue_01_1k.obj").string(), "", options, &mesh);
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
            bool    res  = TriMesh::LoadOBJ(GetAssetPath("models/horse_statue_01_1k_LOD_1.obj").string(), "", options, &mesh);
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
            bool    res  = TriMesh::LoadOBJ(GetAssetPath("models/horse_statue_01_1k_LOD_2.obj").string(), "", options, &mesh);
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
            bool    res  = TriMesh::LoadOBJ(GetAssetPath("models/horse_statue_01_1k_LOD_3.obj").string(), "", options, &mesh);
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
            bool    res  = TriMesh::LoadOBJ(GetAssetPath("models/horse_statue_01_1k_LOD_4.obj").string(), "", options, &mesh);
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
    std::vector<float3>          combinedMeshPositions;
    std::vector<meshopt_Meshlet> combinedMeshlets;
    std::vector<uint32_t>        combinedMeshletVertices;
    std::vector<uint8_t>         combinedMeshletTriangles;
    std::vector<uint32_t>        meshlet_LOD_Offsets;
    std::vector<uint32_t>        meshlet_LOD_Counts;

    for (auto& mesh : meshLODs)
    {
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
            sizeof(float3),
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
        bool res = mesh.GetPositions() == combinedMeshPositions;

        for (auto meshlet : meshlets)
        {
            meshlet.vertex_offset += meshletVertexOffset;
            meshlet.triangle_offset += meshletTriangleOffset;
            combinedMeshlets.push_back(meshlet);
        }
        //res = meshlets == combinedMeshlets;

        for (auto vertex : meshletVertices)
        {
            vertex += vertexOffset;
            combinedMeshletVertices.push_back(vertex);
        }
        res = meshletVertices == combinedMeshletVertices;

        std::copy(meshletTriangles.begin(), meshletTriangles.end(), std::back_inserter(combinedMeshletTriangles));
        res = meshletTriangles == combinedMeshletTriangles;
    }

    // Meshlet bounds (we're using bounding spheres)
    std::vector<float4> meshletBounds;
    for (auto& meshlet : combinedMeshlets)
    {
        auto bounds = meshopt_computeMeshletBounds(
            &combinedMeshletVertices[meshlet.vertex_offset],
            &combinedMeshletTriangles[meshlet.triangle_offset],
            meshlet.triangle_count,
            reinterpret_cast<const float*>(combinedMeshPositions.data()),
            combinedMeshPositions.size(),
            sizeof(float3));
        meshletBounds.push_back(float4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius));
    }

    // Get some counts to use later
    uint64_t combinedMeshletVertexCount   = 0;
    uint64_t combinedMeshletTriangleCount = 0;
    for (auto& m : combinedMeshlets)
    {
        combinedMeshletVertexCount += m.vertex_count;
        combinedMeshletTriangleCount += m.triangle_count;
    }

    // Repack
    std::vector<uint32_t> meshletTrianglesU32;
    for (auto& m : combinedMeshlets)
    {
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
    }

    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> meshletBuffer;
    ComPtr<ID3D12Resource> meshletVerticesBuffer;
    ComPtr<ID3D12Resource> meshletTrianglesBuffer;
    ComPtr<ID3D12Resource> meshletBoundsBuffer;
    {
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshPositions), DataPtr(combinedMeshPositions), D3D12_HEAP_TYPE_UPLOAD, &positionBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshlets), DataPtr(combinedMeshlets), D3D12_HEAP_TYPE_UPLOAD, &meshletBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshletVertices), DataPtr(combinedMeshletVertices), D3D12_HEAP_TYPE_UPLOAD, &meshletVerticesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletTrianglesU32), DataPtr(meshletTrianglesU32), D3D12_HEAP_TYPE_UPLOAD, &meshletTrianglesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletBounds), DataPtr(meshletBounds), D3D12_HEAP_TYPE_UPLOAD, &meshletBoundsBuffer));
    }

    // *************************************************************************
    // Root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature>
        rootSig;
    CreateGlobalRootSig(renderer.get(), &rootSig);

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    ComPtr<ID3D12PipelineState> pipelineState;
    CHECK_CALL(CreateMeshShaderPipeline(renderer.get(), rootSig.Get(), dxilAS, dxilMS, dxilPS, GREX_DEFAULT_RTV_FORMAT, GREX_DEFAULT_DSV_FORMAT, &pipelineState));

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, GREX_BASE_FILE_NAME());
    if (!window)
    {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForD3D12(renderer.get()))
    {
        assert(false && "Window::InitImGuiForD3D12 failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command allocator
    // *************************************************************************
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    {
        CHECK_CALL(renderer->Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,    // type
            IID_PPV_ARGS(&commandAllocator))); // ppCommandList
    }

    // *************************************************************************
    // Command list
    // *************************************************************************
    ComPtr<ID3D12GraphicsCommandList6> commandList;
    {
        CHECK_CALL(renderer->Device->CreateCommandList1(
            0,                              // nodeMask
            D3D12_COMMAND_LIST_TYPE_DIRECT, // type
            D3D12_COMMAND_LIST_FLAG_NONE,   // flags
            IID_PPV_ARGS(&commandList)));   // ppCommandList
    }

    // *************************************************************************
    // Pipeline statistics
    // *************************************************************************
    ComPtr<ID3D12QueryHeap> queryHeap;
    {
        D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1, 1};
        CHECK_CALL(renderer->Device->CreateQueryHeap(&desc, IID_PPV_ARGS(&queryHeap)));
    }

    ComPtr<ID3D12Resource> queryBuffer;
    CHECK_CALL(CreateBuffer(renderer.get(), sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1), D3D12_HEAP_TYPE_READBACK, &queryBuffer));
    //
    bool hasPiplineStats = false;

    // *************************************************************************
    // Scene and constant buffer
    // *************************************************************************
    SceneProperties scene = {};

    ComPtr<ID3D12Resource> sceneBuffer;
    {
        size_t size = Align<size_t>(sizeof(SceneProperties), 256);
        CHECK_CALL(CreateBuffer(renderer.get(), size, D3D12_HEAP_TYPE_UPLOAD, &sceneBuffer));
    }

    // *************************************************************************
    // Instances
    // *************************************************************************
    const uint32_t        kNumInstanceCols = 1;
    const uint32_t        kNumInstanceRows = 5;
    std::vector<float4x4> instances(kNumInstanceCols * kNumInstanceRows);

    ComPtr<ID3D12Resource> instancesBuffer;
    CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(instances), D3D12_HEAP_TYPE_UPLOAD, &instancesBuffer));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents())
    {
        // ---------------------------------------------------------------------

        D3D12_QUERY_DATA_PIPELINE_STATISTICS1 pipelineStatistics = {};
        if (hasPiplineStats)
        {
            void* ptr = nullptr;
            CHECK_CALL(queryBuffer->Map(0, nullptr, &ptr));
            memcpy(&pipelineStatistics, ptr, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1));
            queryBuffer->Unmap(0, nullptr);
        }

        // ---------------------------------------------------------------------
        window->ImGuiNewFrameD3D12();

        if (ImGui::Begin("Params"))
        {
            // Visibility Func
            static const char* currentVisibilityFuncName = gVisibilityFuncNames[gVisibilityFunc].c_str();
            if (ImGui::BeginCombo("Visibility Func", currentVisibilityFuncName))
            {
                for (size_t i = 0; i < gVisibilityFuncNames.size(); ++i)
                {
                    bool isSelected = (currentVisibilityFuncName == gVisibilityFuncNames[i]);
                    if (ImGui::Selectable(gVisibilityFuncNames[i].c_str(), isSelected))
                    {
                        currentVisibilityFuncName = gVisibilityFuncNames[i].c_str();
                        gVisibilityFunc           = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Checkbox("Fit Cone to Far Clip", &gFitConeToFarClip);

            ImGui::Separator();

            auto combinedMeshletCount       = combinedMeshlets.size();
            auto instanceCount              = instances.size();
            auto totalMeshletCount          = combinedMeshletCount * instanceCount;
            auto totalMeshletVertexCount    = combinedMeshletVertexCount * instanceCount;
            auto totalMeshletPrimitiveCount = combinedMeshletTriangleCount * instanceCount;

            ImGui::Columns(2);
            // clang-format off
            ImGui::Text("Combined Meshlet Count");            ImGui::NextColumn(); ImGui::Text("%d", combinedMeshletCount); ImGui::NextColumn();
            ImGui::Text("Combined Meshlet Vertex Count");     ImGui::NextColumn(); ImGui::Text("%d", combinedMeshletVertexCount); ImGui::NextColumn();
            ImGui::Text("Combined Meshlet Primitive Count");  ImGui::NextColumn(); ImGui::Text("%d", combinedMeshletTriangleCount); ImGui::NextColumn();
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
            ImGui::Text("IAVertices");    ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.IAVertices); ImGui::NextColumn();                
            ImGui::Text("IAPrimitives");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.IAPrimitives); ImGui::NextColumn();                
            ImGui::Text("VSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.VSInvocations); ImGui::NextColumn();                
            ImGui::Text("GSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.GSInvocations); ImGui::NextColumn();                
            ImGui::Text("GSPrimitives");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.GSPrimitives); ImGui::NextColumn();                
            ImGui::Text("CInvocations");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.CInvocations); ImGui::NextColumn();                
            ImGui::Text("CPrimitives");   ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.CPrimitives); ImGui::NextColumn();                
            ImGui::Text("PSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.PSInvocations); ImGui::NextColumn();                
            ImGui::Text("HSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.HSInvocations); ImGui::NextColumn();                
            ImGui::Text("DSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.DSInvocations); ImGui::NextColumn();                
            ImGui::Text("CSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.CSInvocations); ImGui::NextColumn();                
            ImGui::Text("ASInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.ASInvocations); ImGui::NextColumn();                
            ImGui::Text("MSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.MSInvocations); ImGui::NextColumn();                
            ImGui::Text("MSPrimitives");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.MSPrimitives); ImGui::NextColumn();
            // clang-format on
            ImGui::Columns(1);
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
                float3 P     = float3(0, 0, -static_cast<float>(0 * instanceSpanZ));
                instances[0] = glm::translate(P) * glm::rotate(t, float3(0, 1, 0));
            }

            // 1
            {
                float3 P     = float3(0, 0, -static_cast<float>(0.75f * instanceSpanZ));
                instances[1] = glm::translate(P) * glm::rotate(t, float3(0, 1, 0));
            }

            // 2
            {
                float3 P     = float3(0, 0, -static_cast<float>(2.5 * instanceSpanZ));
                instances[2] = glm::translate(P) * glm::rotate(t, float3(0, 1, 0));
            }

            // 3
            {
                float3 P     = float3(0, 0, -static_cast<float>(8 * instanceSpanZ));
                instances[3] = glm::translate(P) * glm::rotate(t, float3(0, 1, 0));
            }

            // 4
            {
                float3 P     = float3(0, 0, -static_cast<float>(40 * instanceSpanZ));
                instances[4] = glm::translate(P) * glm::rotate(t, float3(0, 1, 0));
            }
        }

        // ---------------------------------------------------------------------

        // Update scene
        {
            float3 eyePosition = float3(0.3f, 0.125f, 0.525f);
            float3 target      = float3(0, 0.1f, -0.425f);

            PerspCamera camera = PerspCamera(45.0f, window->GetAspectRatio(), 0.1f, farDist);
            camera.LookAt(eyePosition, target);

            Camera::FrustumPlane frLeft, frRight, frTop, frBottom, frNear, frFar;
            camera.GetFrustumPlanes(&frLeft, &frRight, &frTop, &frBottom, &frNear, &frFar);
            //
            auto frCone = camera.GetFrustumCone(gFitConeToFarClip);

            scene.CameraVP                             = camera.GetViewProjectionMatrix();
            scene.Frustum.Planes[FRUSTUM_PLANE_LEFT]   = {frLeft.Normal, 0.0f, frLeft.Position, 0.0f};
            scene.Frustum.Planes[FRUSTUM_PLANE_RIGHT]  = {frRight.Normal, 0.0f, frRight.Position, 0.0f};
            scene.Frustum.Planes[FRUSTUM_PLANE_TOP]    = {frTop.Normal, 0.0f, frTop.Position, 0.0f};
            scene.Frustum.Planes[FRUSTUM_PLANE_BOTTOM] = {frBottom.Normal, 0.0f, frBottom.Position, 0.0f};
            scene.Frustum.Planes[FRUSTUM_PLANE_NEAR]   = {frNear.Normal, 0.0f, frNear.Position, 0.0f};
            scene.Frustum.Planes[FRUSTUM_PLANE_FAR]    = {frFar.Normal, 0.0f, frFar.Position, 0.0f};
            scene.Frustum.Sphere                       = camera.GetFrustumSphere();
            scene.Frustum.Cone.Tip                     = frCone.Tip;
            scene.Frustum.Cone.Height                  = frCone.Height;
            scene.Frustum.Cone.Direction               = frCone.Dir;
            scene.Frustum.Cone.Angle                   = frCone.Angle;
            scene.InstanceCount                        = static_cast<uint32_t>(instances.size());
            scene.MeshletCount                         = meshlet_LOD_Counts[0];
            scene.VisibilityFunc                       = gVisibilityFunc;
            scene.Meshlet_LOD_Offsets[0].x             = meshlet_LOD_Offsets[0];
            scene.Meshlet_LOD_Offsets[1].x             = meshlet_LOD_Offsets[1];
            scene.Meshlet_LOD_Offsets[2].x             = meshlet_LOD_Offsets[2];
            scene.Meshlet_LOD_Offsets[3].x             = meshlet_LOD_Offsets[3];
            scene.Meshlet_LOD_Offsets[4].x             = meshlet_LOD_Offsets[4];
            scene.Meshlet_LOD_Counts[0].x              = meshlet_LOD_Counts[0];
            scene.Meshlet_LOD_Counts[1].x              = meshlet_LOD_Counts[1];
            scene.Meshlet_LOD_Counts[2].x              = meshlet_LOD_Counts[2];
            scene.Meshlet_LOD_Counts[3].x              = meshlet_LOD_Counts[3];
            scene.Meshlet_LOD_Counts[4].x              = meshlet_LOD_Counts[4];

            void* pDst = nullptr;
            CHECK_CALL(sceneBuffer->Map(0, nullptr, &pDst));
            memcpy(pDst, &scene, sizeof(SceneProperties));
            sceneBuffer->Unmap(0, nullptr);
        }

        // ---------------------------------------------------------------------

        // Copy instances transforms to instances buffer
        {
            void* pDst = nullptr;
            CHECK_CALL(instancesBuffer->Map(0, nullptr, &pDst));
            memcpy(pDst, instances.data(), SizeInBytes(instances));
            instancesBuffer->Unmap(0, nullptr);
        }

        // ---------------------------------------------------------------------

        UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

        ComPtr<ID3D12Resource> swapchainBuffer;
        CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        D3D12_RESOURCE_BARRIER preRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &preRenderBarrier);
        {
            commandList->OMSetRenderTargets(
                1,
                &renderer->SwapchainRTVDescriptorHandles[bufferIndex],
                false,
                &renderer->SwapchainDSVDescriptorHandles[bufferIndex]);

            float clearColor[4] = {0.23f, 0.23f, 0.31f, 0};
            commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 0, nullptr);
            commandList->ClearDepthStencilView(renderer->SwapchainDSVDescriptorHandles[bufferIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 0, nullptr);

            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

            commandList->SetGraphicsRootSignature(rootSig.Get());
            commandList->SetPipelineState(pipelineState.Get());

            commandList->SetGraphicsRootConstantBufferView(0, sceneBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(1, positionBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(2, meshletBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(3, meshletBoundsBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(4, meshletVerticesBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(5, meshletTrianglesBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(6, instancesBuffer->GetGPUVirtualAddress());

            // DispatchMesh with pipeline statistics
            {
                commandList->BeginQuery(queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);

                // Amplification shader uses 32 for thread group size
                UINT threadGroupCountX = static_cast<UINT>((meshlet_LOD_Counts[0] / 32 + 1) * instances.size());
                commandList->DispatchMesh(threadGroupCountX, 1, 1);

                commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);
            }

            // Resolve query
            commandList->ResolveQueryData(queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0, 1, queryBuffer.Get(), 0);

            // ImGui
            window->ImGuiRenderDrawData(renderer.get(), commandList.Get());
        }
        D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &postRenderBarrier);

        commandList->Close();

        ID3D12CommandList* pList = commandList.Get();
        renderer->Queue->ExecuteCommandLists(1, &pList);

        if (!WaitForGpu(renderer.get()))
        {
            assert(false && "WaitForGpu failed");
            break;
        }

        // Command list execution is done we can read the pipeline stats
        hasPiplineStats = true;

        // Present
        if (!SwapchainPresent(renderer.get()))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    std::vector<D3D12_ROOT_PARAMETER> rootParameters;

    // ConstantBuffer<SceneProperties> Scene : register(b0);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.Descriptor.ShaderRegister = 0;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<Vertex> Vertices : register(t1);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 1;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<Meshlet> Meshlets : register(t2);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 2;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<float4> MeshletBounds : register(t3);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 3;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<uint> VertexIndices : register(t4);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 4;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<Instance> TriangleIndices : register(t5);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 5;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // ByteAddressBuffer Instances : register(t6);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 6;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters.push_back(rootParameter);
    }

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = static_cast<UINT>(rootParameters.size());
    rootSigDesc.pParameters               = rootParameters.data();
    rootSigDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    CHECK_CALL(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    CHECK_CALL(pRenderer->Device->CreateRootSignature(
        0,                         // nodeMask
        blob->GetBufferPointer(),  // pBloblWithRootSignature
        blob->GetBufferSize(),     // blobLengthInBytes
        IID_PPV_ARGS(ppRootSig))); // riid, ppvRootSignature
}
