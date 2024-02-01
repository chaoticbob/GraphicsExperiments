#include "glfm.h"

#include "mtl_renderer.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

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
// Shader code
// =============================================================================
const char* gShaders = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
	float4 PositionCS [[position]];
	float3 Color;
};

using Mesh = metal::mesh<Vertex, void, 3, 1, topology::triangle>;

[[mesh]]
void meshMain(Mesh outMesh)
{
    outMesh.set_primitive_count(3);
    
    Vertex vertices[3];
    
    vertices[0].PositionCS = float4(-0.5, 0.5, 0.0, 1.0);
    vertices[0].Color = float3(1.0, 0.0, 0.0);

    vertices[1].PositionCS = float4(0.5, 0.5, 0.0, 1.0);
    vertices[1].Color = float3(0.0, 1.0, 0.0);

    vertices[2].PositionCS = float4(0.0, -0.5, 0.0, 1.0);
    vertices[2].Color = float3(0.0, 0.0, 1.0);
    
    outMesh.set_vertex(0, vertices[0]);
    outMesh.set_vertex(1, vertices[1]);
    outMesh.set_vertex(2, vertices[2]);
    
    outMesh.set_index(0, 0);
    outMesh.set_index(1, 1);
    outMesh.set_index(2, 2);
}

struct FSInput
{
    Vertex vtx;
};

[[fragment]]
float4 fragmentMain(FSInput input [[stage_in]])
{
	return float4(input.vtx.Color, 1.0);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 0;
static uint32_t gWindowHeight = 0;
static bool     gEnableDebug  = true;

// =============================================================================
//App
// =============================================================================
struct App {
    std::unique_ptr<MetalRenderer> renderer;

    MetalPipelineRenderState renderPipelineState;
};

static std::unique_ptr<App> gApp;

static void onSurfaceCreated(GLFMDisplay* pDisplay, int width, int height);
static void onRender(GLFMDisplay* pDisplay);

// =============================================================================
// main()
// =============================================================================
void glfmMain(GLFMDisplay* pDisplay)
{
    glfmSetDisplayConfig(
        pDisplay,
        GLFMRenderingAPIMetal,
        GLFMColorFormatRGBA8888,
        GLFMDepthFormat24,
        GLFMStencilFormatNone,
        GLFMMultisampleNone);

    glfmSetSurfaceCreatedFunc(pDisplay, onSurfaceCreated);
    glfmSetRenderFunc(pDisplay, onRender);
}

static void onSurfaceCreated(GLFMDisplay* pDisplay, int width, int height)
{
    gApp = std::make_unique<App>();

    gApp->renderer = std::make_unique<MetalRenderer>();
    InitMetal(gApp->renderer.get(), gEnableDebug, glfmGetMetalView(pDisplay));

    gWindowWidth = static_cast<uint32_t>(width);
    gWindowHeight = static_cast<uint32_t>(height);

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    MetalShader msShader;
    MetalShader fsShader;
    NS::Error*  pError  = nullptr;
    auto        library = NS::TransferPtr(gApp->renderer->Device->newLibrary(
        NS::String::string(gShaders, NS::UTF8StringEncoding),
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
    
    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    // Render pipeline state
    {
        auto desc = NS::TransferPtr(MTL::MeshRenderPipelineDescriptor::alloc()->init());
        if (!desc) {
            assert(false && "MTL::MeshRenderPipelineDescriptor::alloc::init() failed");
            return EXIT_FAILURE;
        }
        
        desc->setMeshFunction(msShader.Function.get());
        desc->setFragmentFunction(fsShader.Function.get());
        desc->colorAttachments()->object(0)->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);
        desc->setDepthAttachmentPixelFormat(GREX_DEFAULT_DSV_FORMAT);

        NS::Error* pError           = nullptr;
        gApp->renderPipelineState.State = NS::TransferPtr(gApp->renderer->Device->newRenderPipelineState(desc.get(), MTL::PipelineOptionNone, nullptr, &pError));
        if (gApp->renderPipelineState.State.get() == nullptr) {
            assert(false && "MTL::Device::newRenderPipelineState() failed");
            return EXIT_FAILURE;
        }
    }
}

static void onRender(GLFMDisplay* pDisplay)
{
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);

    MTK::View* pView = static_cast<MTK::View*>(glfmGetMetalView(pDisplay));
    
    auto pRenderPassDescriptor = pView->currentRenderPassDescriptor();
    pRenderPassDescriptor->colorAttachments()->object(0)->setClearColor(clearColor);

    MTL::CommandBuffer*        pCommandBuffer = gApp->renderer->Queue->commandBuffer();
    MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);

    pRenderEncoder->setRenderPipelineState(gApp->renderPipelineState.State.get());

    // No object function, so all zeros for threadsPerObjectThreadgroup
    pRenderEncoder->drawMeshThreadgroups(MTL::Size(1, 1, 1), MTL::Size(0, 0, 0), MTL::Size(1, 1, 1));
    
    pRenderEncoder->endEncoding();
    
    pCommandBuffer->presentDrawable(pView->currentDrawable());
    pCommandBuffer->commit();
        
    glfmSwapBuffers(pDisplay);
}
