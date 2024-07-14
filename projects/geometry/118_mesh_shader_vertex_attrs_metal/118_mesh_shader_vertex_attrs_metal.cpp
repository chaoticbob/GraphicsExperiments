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
        if (pError != nullptr) {                                                     \
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

struct SceneProperties {
    float4x4 InstanceM;
    float4x4 CameraVP;
    float3   EyePosition;
    uint     DrawFunc;
    float3   LightPosition;
    uint     __pad0;
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

enum DrawFunc {
    DRAW_FUNC_POSITION  = 0,
    DRAW_FUNC_TEX_COORD = 1,
    DRAW_FUNC_NORMAL    = 2,
    DRAW_FUNC_PHONG     = 3,
};

static std::vector<std::string> gDrawFuncNames = {
    "Position",
    "Tex Coord",
    "Normal",
    "Phong",
};

static int gDrawFunc = DRAW_FUNC_PHONG;

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<MetalRenderer> renderer = std::make_unique<MetalRenderer>();

    if (!InitMetal(renderer.get(), gEnableDebug)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    MetalShader osShader;
    MetalShader msShader;
    MetalShader fsShader;
    NS::Error*  pError  = nullptr;
    {
        std::string shaderSource = LoadString("projects/118_mesh_shader_vertex_attrs/shaders.metal");
        if (shaderSource.empty()) {
            assert(false && "no shader source");
            return EXIT_FAILURE;
        }

        auto library = NS::TransferPtr(renderer->Device->newLibrary(
            NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding),
            nullptr,
            &pError));

        if (library.get() == nullptr) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error: " << pError->localizedDescription()->utf8String() << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        osShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("objectMain", NS::UTF8StringEncoding)));
        if (osShader.Function.get() == nullptr) {
            assert(false && "OS MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        msShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("meshMain", NS::UTF8StringEncoding)));
        if (msShader.Function.get() == nullptr) {
            assert(false && "MS MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        fsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("fragmentMain", NS::UTF8StringEncoding)));
        if (fsShader.Function.get() == nullptr) {
            assert(false && "FS MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }
    }
    
    // *************************************************************************
    // Make them meshlets!
    // *************************************************************************
    std::vector<glm::vec3>       positions;
    std::vector<glm::vec2>       texCoords;
    std::vector<glm::vec3>       normals;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t>        meshletVertices;
    std::vector<uint8_t>         meshletTriangles;
    {
        TriMesh mesh = {};
        bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/full_horse_statue_01_1k.obj").string(), &mesh);
        if (!res)
        {
            assert(false && "failed to load model");
        }

        positions = mesh.GetPositions();
        texCoords = mesh.GetTexCoords();
        normals   = mesh.GetNormals();

        const size_t kMaxVertices  = 64;
        const size_t kMaxTriangles = 124;
        const float  kConeWeight   = 0.0f;

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
            sizeof(glm::vec3),
            kMaxVertices,
            kMaxTriangles,
            kConeWeight);

        auto& last = meshlets[meshletCount - 1];
        meshletVertices.resize(last.vertex_offset + last.vertex_count);
        meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshletCount);
    }

    // Repack triangles from 3 consecutive byes to 4-byte uint32_t to 
    // make it easier to unpack on the GPU.
    //
    std::vector<uint32_t> meshletTrianglesU32;
    for (auto& m : meshlets)
    {
        // Save triangle offset for current meshlet
        uint32_t triangleOffset = static_cast<uint32_t>(meshletTrianglesU32.size());

        // Repack to uint32_t
        for (uint32_t i = 0; i < m.triangle_count; ++i)
        {
            uint32_t i0 = 3 * i + 0 + m.triangle_offset;
            uint32_t i1 = 3 * i + 1 + m.triangle_offset;
            uint32_t i2 = 3 * i + 2 + m.triangle_offset;

            uint8_t  vIdx0  = meshletTriangles[i0];
            uint8_t  vIdx1  = meshletTriangles[i1];
            uint8_t  vIdx2  = meshletTriangles[i2];
            uint32_t packed = ((static_cast<uint32_t>(vIdx0) & 0xFF) << 0) |
                              ((static_cast<uint32_t>(vIdx1) & 0xFF) << 8) |
                              ((static_cast<uint32_t>(vIdx2) & 0xFF) << 16);
            meshletTrianglesU32.push_back(packed);
        }

        // Update triangle offset for current meshlet
        m.triangle_offset = triangleOffset;
    }
    
    MetalBuffer positionBuffer;
    MetalBuffer texCoordsBuffer;
    MetalBuffer normalsBuffer;
    MetalBuffer meshletBuffer;
    MetalBuffer meshletVerticesBuffer;
    MetalBuffer meshletTrianglesBuffer;
    {
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(positions), DataPtr(positions), &positionBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(texCoords), DataPtr(texCoords), &texCoordsBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(normals), DataPtr(normals), &normalsBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshlets), DataPtr(meshlets), &meshletBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletVertices), DataPtr(meshletVertices), &meshletVerticesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletTrianglesU32), DataPtr(meshletTrianglesU32), &meshletTrianglesBuffer));
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
            if (!desc) {
                assert(false && "MTL::MeshRenderPipelineDescriptor::alloc::init() failed");
                return EXIT_FAILURE;        
            }
            
            desc->setObjectFunction(osShader.Function.get());
            desc->setMeshFunction(msShader.Function.get());
            desc->setFragmentFunction(fsShader.Function.get());
            desc->colorAttachments()->object(0)->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
            desc->setDepthAttachmentPixelFormat(GREX_DEFAULT_DSV_FORMAT);

            NS::Error* pError           = nullptr;
            renderPipelineState.State = NS::TransferPtr(renderer->Device->newRenderPipelineState(desc.get(), MTL::PipelineOptionNone, nullptr, &pError));
            if (renderPipelineState.State.get() == nullptr) {
                assert(false && "MTL::Device::newRenderPipelineState() failed");
                return EXIT_FAILURE;
            }
        }
        
        // Depth stencil state
        {
            auto desc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
            if (!desc) {
                assert(false && "MTL::DepthStencilDescriptor::alloc::init() failed");
                return EXIT_FAILURE;        
            }

            if (desc.get() != nullptr) {
                desc->setDepthCompareFunction(MTL::CompareFunctionLess);
                desc->setDepthWriteEnabled(true);

                depthStencilState.State = NS::TransferPtr(renderer->Device->newDepthStencilState(desc.get()));
                if (depthStencilState.State.get() == nullptr) {
                    assert(false && "MTL::Device::newDepthStencilState() failed");
                    return EXIT_FAILURE;
                }
            }
        }
    }
    
    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight,  GREX_BASE_FILE_NAME());
    if (!window) {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Render Pass Description
    // *************************************************************************
    MTL::RenderPassDescriptor* pRenderPassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float)) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get())) {
        assert(false && "GrexWindow::InitImGuiForMetal failed");
        return EXIT_FAILURE;
    }
    
    // *************************************************************************
    // Scene
    // *************************************************************************
    SceneProperties scene = {};    

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents()) {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);
        
        if (ImGui::Begin("Params")) {
            // Visibility Func
            static const char* currentDrawFuncName = gDrawFuncNames[gDrawFunc].c_str();
            if (ImGui::BeginCombo("Draw Func", currentDrawFuncName))
            {
                for (size_t i = 0; i < gDrawFuncNames.size(); ++i)
                {
                    bool isSelected = (currentDrawFuncName == gDrawFuncNames[i]);
                    if (ImGui::Selectable(gDrawFuncNames[i].c_str(), isSelected))
                    {
                        currentDrawFuncName = gDrawFuncNames[i].c_str();
                        gDrawFunc           = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::End();
        
        // ---------------------------------------------------------------------
        
        // Update scene
        {
            float3 eyePosition = float3(0, 0.105f, 0.40f);
            float3 target      = float3(0, 0.105f, 0);

            PerspCamera camera = PerspCamera(60.0f, window->GetAspectRatio(), 0.1f, 10000.0f);
            camera.LookAt(eyePosition, target);

            scene.InstanceM     = glm::rotate(static_cast<float>(glfwGetTime()), glm::vec3(0, 1, 0));
            scene.CameraVP      = camera.GetViewProjectionMatrix();
            scene.EyePosition   = eyePosition;
            scene.DrawFunc      = gDrawFunc;
            scene.LightPosition = float3(0.25f, 1, 1);
        }

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
        pRenderEncoder->setMeshBytes(&scene, sizeof(SceneProperties), 0);
        pRenderEncoder->setMeshBuffer(positionBuffer.Buffer.get(), 0, 1);
        pRenderEncoder->setMeshBuffer(texCoordsBuffer.Buffer.get(), 0, 2);
        pRenderEncoder->setMeshBuffer(normalsBuffer.Buffer.get(), 0, 3);
        pRenderEncoder->setMeshBuffer(meshletBuffer.Buffer.get(), 0, 4);
        pRenderEncoder->setMeshBuffer(meshletVerticesBuffer.Buffer.get(), 0, 5);
        pRenderEncoder->setMeshBuffer(meshletTrianglesBuffer.Buffer.get(), 0, 6);

        pRenderEncoder->setFragmentBytes(&scene, sizeof(SceneProperties), 0);

        // Object function uses 32 for thread group size
        uint32_t threadGroupCountX = static_cast<uint32_t>((meshlets.size() / 32) + 1);
        pRenderEncoder->drawMeshThreadgroups(MTL::Size(threadGroupCountX, 1, 1), MTL::Size(32, 1, 1), MTL::Size(128, 1, 1));
        
        // Draw ImGui
        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);
        
        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}
