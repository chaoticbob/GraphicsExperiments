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
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

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
        std::string shaderSource = LoadString("projects/113_mesh_shader_instancing/shaders.metal");
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
    // **************************************************************************
    TriMesh::Aabb                meshBounds = {};
    std::vector<glm::vec3>       positions;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t>        meshletVertices;
    std::vector<uint8_t>         meshletTriangles;
    {
        TriMesh mesh = {};
        bool    res  = TriMesh::LoadOBJ2(GetAssetPath("models/horse_statue_01_1k.obj").string(), &mesh);
        if (!res)
        {
            assert(false && "failed to load model");
        }
        
        meshBounds = mesh.GetBounds();
        positions = mesh.GetPositions();

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
    MetalBuffer meshletBuffer;
    MetalBuffer meshletVerticesBuffer;
    MetalBuffer meshletTrianglesBuffer;
    {
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(positions), DataPtr(positions), &positionBuffer));
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
    auto window = Window::Create(gWindowWidth, gWindowHeight,  GREX_BASE_FILE_NAME());
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Render Pass Description
    // *************************************************************************
    MTL::RenderPassDescriptor* pRenderPassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float)) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }
    
    // *************************************************************************
    // Counter statistics - revisit later!
    // *************************************************************************
    
    // *************************************************************************
    // Instances
    // *************************************************************************
    const uint32_t    kNumInstanceCols = 20;
    const uint32_t    kNumInstanceRows = 10;
    std::vector<mat4> instances(kNumInstanceCols * kNumInstanceRows);

    MetalBuffer instancesBuffer;
    CreateBuffer(renderer.get(), SizeInBytes(instances), nullptr, &instancesBuffer);

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents()) {
        // ---------------------------------------------------------------------

        // Update instance transforms
        {
            float maxSpan       = std::max<float>(meshBounds.Width(), meshBounds.Depth());
            float instanceSpanX = 2.0f * maxSpan;
            float instanceSpanZ = 4.5f * maxSpan;
            float totalSpanX    = kNumInstanceCols * instanceSpanX;
            float totalSpanZ    = kNumInstanceRows * instanceSpanZ;

            for (uint32_t j = 0; j < kNumInstanceRows; ++j)
            {
                for (uint32_t i = 0; i < kNumInstanceCols; ++i)
                {
                    float x = i * instanceSpanX - (totalSpanX / 2.0f) + instanceSpanX / 2.0f;
                    float y = 0;
                    float z = j * instanceSpanZ - (totalSpanZ / 2.0f) - 2.15f * instanceSpanZ;

                    uint32_t index   = j * kNumInstanceCols + i;
                    float    t       = static_cast<float>(glfwGetTime()) + ((i ^ j + i) / 10.0f);
                    instances[index] = glm::translate(glm::vec3(x, y, z)) * glm::rotate(t, glm::vec3(0, 1, 0));
                }
            }
        }

        // ---------------------------------------------------------------------

        // Copy instances transforms to instances buffer
        {
            memcpy(instancesBuffer.Buffer->contents(), DataPtr(instances), SizeInBytes(instances));
            instancesBuffer.Buffer->didModifyRange(NS::Range::Make(0, SizeInBytes(instances)));
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

        PerspCamera camera = PerspCamera(45.0f, window->GetAspectRatio(), 0.1f, 1000.0f);
        camera.LookAt(vec3(0, 0.7f, 3.0f), vec3(0, 0.105f, 0));

        mat4 VP = camera.GetViewProjectionMatrix();
        uint32_t instanceCount = static_cast<uint32_t>(instances.size());
        uint32_t meshletCount = static_cast<uint32_t>(meshlets.size());

        //
        // Use a struct since metalcpp doesn't seem to expose a
        // variant of set*Bytes with an offset currently.
        //
        struct SceneProperties {
            mat4 CameraVP;
            uint InstanceCount;
            uint MeshletCount;
            uint pad[2];
        };
        SceneProperties scene = {VP, instanceCount, meshletCount};

        pRenderEncoder->setObjectBytes(&scene, sizeof(SceneProperties), 0);
        pRenderEncoder->setMeshBytes(&scene, sizeof(SceneProperties), 0);
        pRenderEncoder->setMeshBuffer(positionBuffer.Buffer.get(), 0, 1);
        pRenderEncoder->setMeshBuffer(meshletBuffer.Buffer.get(), 0, 2);
        pRenderEncoder->setMeshBuffer(meshletVerticesBuffer.Buffer.get(), 0, 3);
        pRenderEncoder->setMeshBuffer(meshletTrianglesBuffer.Buffer.get(), 0, 4);
        pRenderEncoder->setMeshBuffer(instancesBuffer.Buffer.get(), 0, 5);

        // Object function uses 32 for thread group size
		uint32_t threadGroupCountX = ((meshletCount * instanceCount) / 32) + 1;

        pRenderEncoder->drawMeshThreadgroups(MTL::Size(threadGroupCountX, 1, 1), MTL::Size(32, 1, 1), MTL::Size(128, 1, 1));


        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}
