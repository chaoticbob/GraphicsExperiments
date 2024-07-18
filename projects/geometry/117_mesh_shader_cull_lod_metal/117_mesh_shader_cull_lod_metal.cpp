#include "window.h"
#include "camera.h"
#include "tri_mesh.h"

#include "mtl_renderer.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include "meshoptimizer.h"

#define CHECK_CALL(FN)                                                               \
    {                                                                                \
        NS::Error* pError = FN;                                                      \
        if (pError != nullptr)                                                       \
        {                                                                            \
            std::stringstream ss;                                                    \
            ss << "\n";                                                              \
            ss << "*** FUNCTION CALL FAILED *** \n";                                 \
            ss << "FUNCTION: " << #FN << "\n";                                       \
            ss << "Error: " << pError->localizedDescription()->utf8String() << "\n"; \
            ss << "\n";                                                              \
            GREX_LOG_ERROR(ss.str().c_str());                                        \
            assert(false);                                                           \
        }                                                                            \
    }

// =============================================================================
// Scene Stuff
// =============================================================================
using float3   = glm::vec3;
using float4   = glm::vec4;
using float4x4 = glm::mat4;
using uint4    = glm::uvec4;

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

//
// NOTE: Unlike D3D12 and Vulkan, it looks like Metal arrays are tightly
//       packed for 32-bit scalar types. This means that Meshlet_LOD_Offsets
//       and Meshlet_LOD_Counts are uint here instead of uint4/uvec4.
//
struct SceneProperties
{
    float3      EyePosition;
    uint        __pad0;
    float4x4    CameraVP;
    FrustumData Frustum;
    uint        InstanceCount;
    uint        MeshletCount;
    uint        VisibilityFunc;
    float       MaxLODDistance;         // Use least detail level at or beyond this distance
    uint        Meshlet_LOD_Offsets[5]; // Align array element to 16 bytes
    uint        Meshlet_LOD_Counts[5];  // Align array element to 16 bytes
    float3      MeshBoundsMin;
    float3      MeshBoundsMax;
    uint        EnableLOD;
    uint        __pad1[3]; // Make struct size aligned to 16
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngle = 55.0f;
static float gAngle       = gTargetAngle;

static bool gFitConeToFarClip = true;

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

static int gVisibilityFunc = VISIBILITY_FUNC_CONE_AND_NEAR_PLANE;

static float gMaxLODDistance = 10.0f;

static bool gEnableLOD = true;

// =============================================================================
// Event functions
// =============================================================================
void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT)
    {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;
    }

    prevX = x;
    prevY = y;
}

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<MetalRenderer> renderer = std::make_unique<MetalRenderer>();

    if (!InitMetal(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    MetalShader osShader;
    MetalShader msShader;
    MetalShader fsShader;
    NS::Error*  pError = nullptr;
    {
        std::string shaderSource = LoadString("projects/117_mesh_shader_cull_lod/shaders.metal");
        if (shaderSource.empty())
        {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        auto library = NS::TransferPtr(renderer->Device->newLibrary(
            NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding),
            nullptr,
            &pError));

        if (library.get() == nullptr)
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error: " << pError->localizedDescription()->utf8String() << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        osShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("objectMain", NS::UTF8StringEncoding)));
        if (osShader.Function.get() == nullptr)
        {
            assert(false && "OS MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        msShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("meshMain", NS::UTF8StringEncoding)));
        if (msShader.Function.get() == nullptr)
        {
            assert(false && "MS MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        fsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("fragmentMain", NS::UTF8StringEncoding)));
        if (fsShader.Function.get() == nullptr)
        {
            assert(false && "FS MTL::Library::newFunction() failed");
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
    std::vector<float3>          combinedMeshPositions;
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

    MetalBuffer positionBuffer;
    MetalBuffer meshletBuffer;
    MetalBuffer meshletVerticesBuffer;
    MetalBuffer meshletTrianglesBuffer;
    MetalBuffer meshletBoundsBuffer;
    {
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshPositions), DataPtr(combinedMeshPositions), &positionBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshlets), DataPtr(combinedMeshlets), &meshletBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(combinedMeshletVertices), DataPtr(combinedMeshletVertices), &meshletVerticesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletTrianglesU32), DataPtr(meshletTrianglesU32), &meshletTrianglesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletBounds), DataPtr(meshletBounds), &meshletBoundsBuffer));
    }

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    MetalPipelineRenderState renderPipelineState;
    MetalDepthStencilState   depthStencilState;
    {
        // Render pipeline state
        {
            auto desc = NS::TransferPtr(MTL::MeshRenderPipelineDescriptor::alloc()->init());
            if (!desc)
            {
                assert(false && "MTL::MeshRenderPipelineDescriptor::alloc::init() failed");
                return EXIT_FAILURE;
            }

            desc->setObjectFunction(osShader.Function.get());
            desc->setMeshFunction(msShader.Function.get());
            desc->setFragmentFunction(fsShader.Function.get());
            desc->colorAttachments()->object(0)->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
            desc->setDepthAttachmentPixelFormat(GREX_DEFAULT_DSV_FORMAT);

            NS::Error* pError         = nullptr;
            renderPipelineState.State = NS::TransferPtr(renderer->Device->newRenderPipelineState(desc.get(), MTL::PipelineOptionNone, nullptr, &pError));
            if (renderPipelineState.State.get() == nullptr)
            {
                assert(false && "MTL::Device::newRenderPipelineState() failed");
                return EXIT_FAILURE;
            }
        }

        // Depth stencil state
        {
            auto desc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
            if (!desc)
            {
                assert(false && "MTL::DepthStencilDescriptor::alloc::init() failed");
                return EXIT_FAILURE;
            }

            if (desc.get() != nullptr)
            {
                desc->setDepthCompareFunction(MTL::CompareFunctionLess);
                desc->setDepthWriteEnabled(true);

                depthStencilState.State = NS::TransferPtr(renderer->Device->newDepthStencilState(desc.get()));
                if (depthStencilState.State.get() == nullptr)
                {
                    assert(false && "MTL::Device::newDepthStencilState() failed");
                    return EXIT_FAILURE;
                }
            }
        }
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

    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Render Pass Description
    // *************************************************************************
    MTL::RenderPassDescriptor* pRenderPassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get()))
    {
        assert(false && "GrexWindow::InitImGuiForMetal failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Counter statistics - revisit later!
    // *************************************************************************

    // *************************************************************************
    // Scene
    // *************************************************************************
    SceneProperties scene = {};

    // *************************************************************************
    // Instances
    // *************************************************************************
    const uint32_t        kNumInstanceCols = 40;
    const uint32_t        kNumInstanceRows = 40;
    std::vector<float4x4> instances(kNumInstanceCols * kNumInstanceRows);

    MetalBuffer instancesBuffer;
    CreateBuffer(renderer.get(), SizeInBytes(instances), nullptr, &instancesBuffer);

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents())
    {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

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

            ImGui::Checkbox("Enable LOD", &gEnableLOD);
            ImGui::DragFloat("Max LOD Distance", &gMaxLODDistance, 0.1f, 1.0f, 50.0f);

            ImGui::Separator();

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

            farDist = std::max(totalSpanX, totalSpanZ);

            for (uint32_t j = 0; j < kNumInstanceRows; ++j)
            {
                for (uint32_t i = 0; i < kNumInstanceCols; ++i)
                {
                    float x = i * instanceSpanX - (totalSpanX / 2.0f) + instanceSpanX / 2.0f;
                    float y = 0;
                    float z = j * instanceSpanZ - (totalSpanZ / 2.0f) + instanceSpanZ / 2.0f;

                    uint32_t index   = j * kNumInstanceCols + i;
                    float    t       = static_cast<float>(glfwGetTime()) + ((i ^ j + i) / 10.0f);
                    instances[index] = glm::translate(float3(x, y, z)) * glm::rotate(t, float3(0, 1, 0));
                }
            }
        }

        // ---------------------------------------------------------------------

        // Update scene
        {
            float3 eyePosition = float3(0, 0.2f, 0.0f);
            float3 target      = float3(0, 0.0f, -1.3f);

            // Smooth out the rotation on Y
            gAngle += (gTargetAngle - gAngle) * 0.1f;
            mat4 rotMat = glm::rotate(glm::radians(gAngle), float3(0, 1, 0));
            target      = rotMat * float4(target, 1.0);

            PerspCamera camera = PerspCamera(45.0f, window->GetAspectRatio(), 0.1f, farDist);
            camera.LookAt(eyePosition, target);

            Camera::FrustumPlane frLeft, frRight, frTop, frBottom, frNear, frFar;
            camera.GetFrustumPlanes(&frLeft, &frRight, &frTop, &frBottom, &frNear, &frFar);
            //
            auto frCone = camera.GetFrustumCone(gFitConeToFarClip);

            scene.EyePosition                          = camera.GetEyePosition();
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
            scene.MaxLODDistance                       = gMaxLODDistance;
            scene.Meshlet_LOD_Offsets[0]               = meshlet_LOD_Offsets[0];
            scene.Meshlet_LOD_Offsets[1]               = meshlet_LOD_Offsets[1];
            scene.Meshlet_LOD_Offsets[2]               = meshlet_LOD_Offsets[2];
            scene.Meshlet_LOD_Offsets[3]               = meshlet_LOD_Offsets[3];
            scene.Meshlet_LOD_Offsets[4]               = meshlet_LOD_Offsets[4];
            scene.Meshlet_LOD_Counts[0]                = meshlet_LOD_Counts[0];
            scene.Meshlet_LOD_Counts[1]                = meshlet_LOD_Counts[1];
            scene.Meshlet_LOD_Counts[2]                = meshlet_LOD_Counts[2];
            scene.Meshlet_LOD_Counts[3]                = meshlet_LOD_Counts[3];
            scene.Meshlet_LOD_Counts[4]                = meshlet_LOD_Counts[4];
            scene.MeshBoundsMin                        = float3(meshBounds.min);
            scene.MeshBoundsMax                        = float3(meshBounds.max);
            scene.EnableLOD                            = gEnableLOD;
        }

        // ---------------------------------------------------------------------

        // Copy instances transforms to instances buffer
        memcpy(instancesBuffer.Buffer->contents(), DataPtr(instances), SizeInBytes(instances));

        // ---------------------------------------------------------------------

        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

        uint32_t swapchainIndex = (frameIndex % renderer->SwapchainBufferCount);

        auto colorTargetDesc = NS::TransferPtr(MTL::RenderPassColorAttachmentDescriptor::alloc()->init());
        colorTargetDesc->setClearColor(clearColor);
        colorTargetDesc->setTexture(pDrawable->texture());
        colorTargetDesc->setLoadAction(MTL::LoadActionClear);
        colorTargetDesc->setStoreAction(MTL::StoreActionStore);
        pRenderPassDescriptor->colorAttachments()->setObject(colorTargetDesc.get(), 0);

        auto depthTargetDesc = NS::TransferPtr(MTL::RenderPassDepthAttachmentDescriptor::alloc()->init());
        depthTargetDesc->setClearDepth(1);
        depthTargetDesc->setTexture(renderer->SwapchainDSVBuffers[swapchainIndex].get());
        depthTargetDesc->setLoadAction(MTL::LoadActionClear);
        depthTargetDesc->setStoreAction(MTL::StoreActionDontCare);
        pRenderPassDescriptor->setDepthAttachment(depthTargetDesc.get());

        MTL::CommandBuffer*        pCommandBuffer = renderer->Queue->commandBuffer();
        MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);

        pRenderEncoder->setRenderPipelineState(renderPipelineState.State.get());
        pRenderEncoder->setDepthStencilState(depthStencilState.State.get());

        // Since Metal supports 4kb of constants data - we don't need to change
        // the scene properties to a buffer.
        //
        pRenderEncoder->setObjectBytes(&scene, sizeof(SceneProperties), 0);
        pRenderEncoder->setObjectBuffer(meshletBoundsBuffer.Buffer.get(), 0, 1);
        pRenderEncoder->setObjectBuffer(instancesBuffer.Buffer.get(), 0, 2);
        //
        pRenderEncoder->setMeshBytes(&scene, sizeof(SceneProperties), 0);
        pRenderEncoder->setMeshBuffer(positionBuffer.Buffer.get(), 0, 1);
        pRenderEncoder->setMeshBuffer(meshletBuffer.Buffer.get(), 0, 2);
        pRenderEncoder->setMeshBuffer(meshletBoundsBuffer.Buffer.get(), 0, 3);
        pRenderEncoder->setMeshBuffer(meshletVerticesBuffer.Buffer.get(), 0, 4);
        pRenderEncoder->setMeshBuffer(meshletTrianglesBuffer.Buffer.get(), 0, 5);
        pRenderEncoder->setMeshBuffer(instancesBuffer.Buffer.get(), 0, 6);

        // Object function uses 32 for thread group size
        uint32_t meshletCount      = static_cast<uint32_t>(meshlet_LOD_Counts[0]);
        uint32_t instanceCount     = static_cast<uint32_t>(instances.size());
        uint32_t threadGroupCountX = ((meshletCount * instanceCount) / 32) + 1;
        pRenderEncoder->drawMeshThreadgroups(MTL::Size(threadGroupCountX, 1, 1), MTL::Size(32, 1, 1), MTL::Size(128, 1, 1));

        // Draw ImGui
        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}
