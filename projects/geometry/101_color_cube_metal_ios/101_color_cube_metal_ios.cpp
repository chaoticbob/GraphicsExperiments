#include "glfm.h"

#include "mtl_renderer.h"
#include "tri_mesh.h"

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

struct Camera {
	float4x4 MVP;
};

struct VSOutput {
	float4 PositionCS [[position]];
	float3 Color;
};

struct VertexData {
	float3 PositionOS [[attribute(0)]];
	float3 Color [[attribute(1)]];
};

VSOutput vertex vertexMain(
	VertexData vertexData [[stage_in]],
	constant Camera &Cam [[buffer(2)]])
{
	VSOutput output;
	float3 position = vertexData.PositionOS;
	output.PositionCS = Cam.MVP * float4(position, 1.0f);
	output.Color = vertexData.Color;
	return output;
}

float4 fragment fragmentMain( VSOutput in [[stage_in]] )
{
	return float4(in.Color, 1.0);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 0;
static uint32_t gWindowHeight = 0;
static bool     gEnableDebug  = true;

void CreateGeometryBuffers(
    MetalRenderer* pRenderer,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pVertexColorBuffer);

// =============================================================================
//App
// =============================================================================
struct App {
    std::unique_ptr<MetalRenderer> renderer;

    MetalPipelineRenderState renderPipelineState;
    MetalDepthStencilState   depthStencilState;

    MetalBuffer pIndexBuffer;
    MetalBuffer pPositionBuffer;
    MetalBuffer pVertexColorBuffer;
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
    MetalShader vsShader;
    MetalShader fsShader;
    NS::Error*  pError  = nullptr;
    auto        library = NS::TransferPtr(gApp->renderer->Device->newLibrary(
        NS::String::string(gShaders, NS::UTF8StringEncoding),
        nullptr,
        &pError));

    if (library.get() == nullptr) {
        std::stringstream ss;
        ss << "\n"
           << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
        return EXIT_FAILURE;
    }

    vsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vertexMain", NS::UTF8StringEncoding)));
    if (vsShader.Function.get() == nullptr) {
        assert(false && "VS Shader MTL::Library::newFunction() failed");
        return EXIT_FAILURE;
    }

    fsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("fragmentMain", NS::UTF8StringEncoding)));
    if (fsShader.Function.get() == nullptr) {
        assert(false && "FS Shader MTL::Library::newFunction() failed");
        return EXIT_FAILURE;
    }
    
    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    CHECK_CALL(CreateDrawVertexColorPipeline(
        gApp->renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &gApp->renderPipelineState,
        &gApp->depthStencilState));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    CreateGeometryBuffers(gApp->renderer.get(), &gApp->pIndexBuffer, &gApp->pPositionBuffer, &gApp->pVertexColorBuffer);    
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
    pRenderEncoder->setDepthStencilState(gApp->depthStencilState.State.get());

    // Update the camera model view projection matrix
    mat4 modelMat = rotate(static_cast<float>(glfmGetTime()), vec3(0, 1, 0)) *
                    rotate(static_cast<float>(glfmGetTime()), vec3(1, 0, 0));
    mat4 viewMat = lookAt(vec3(0, 0, 2), vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 projMat = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

    mat4 mvpMat = projMat * viewMat * modelMat;

    pRenderEncoder->setVertexBytes(&mvpMat, sizeof(glm::mat4), 2);

    MTL::Buffer* vbvs[2]    = {gApp->pPositionBuffer.Buffer.get(), gApp->pVertexColorBuffer.Buffer.get()};
    NS::UInteger offsets[2] = {0, 0};
    NS::Range    vbRange(0, 2);
    pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

    pRenderEncoder->drawIndexedPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, 36, MTL::IndexTypeUInt32, gApp->pIndexBuffer.Buffer.get(), 0);

    pRenderEncoder->endEncoding();
    
    pCommandBuffer->presentDrawable(pView->currentDrawable());
    pCommandBuffer->commit();
        
    glfmSwapBuffers(pDisplay);
}

void CreateGeometryBuffers(
    MetalRenderer* pRenderer,
    MetalBuffer*   pIndexBuffer,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pVertexColorBuffer)
{
    TriMesh::Options options;
    options.enableVertexColors = true;

    TriMesh mesh = TriMesh::Cube(vec3(1), false, options);

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        pIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetVertexColors()),
        DataPtr(mesh.GetVertexColors()),
        pVertexColorBuffer));
}
