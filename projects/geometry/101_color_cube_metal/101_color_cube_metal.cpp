#include "window.h"

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
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

void CreateGeometryBuffers(
    MetalRenderer* pRenderer,
    MTL::Buffer**  ppIndexBuffer,
    MTL::Buffer**  ppPositionBuffer,
    MTL::Buffer**  ppVertexColorBuffer);

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
    NS::Error*    pError   = nullptr;
    MTL::Library* pLibrary = renderer->Device->newLibrary(
        NS::String::string(gShaders, NS::UTF8StringEncoding),
        nullptr,
        &pError);

    if (!pLibrary) {
        std::stringstream ss;
        ss << "\n"
           << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
        return EXIT_FAILURE;
    }

    MTL::Function* metalVS = pLibrary->newFunction(NS::String::string("vertexMain", NS::UTF8StringEncoding));
    MTL::Function* metalFS = pLibrary->newFunction(NS::String::string("fragmentMain", NS::UTF8StringEncoding));

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    MTL::RenderPipelineState* pRenderPipelineState = nullptr;
    MTL::DepthStencilState*   pDepthStencilState   = nullptr;
    CHECK_CALL(CreateDrawVertexColorPipeline(
        renderer.get(),
        metalVS,
        metalFS,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pRenderPipelineState,
        &pDepthStencilState));

    metalVS->release();
    metalFS->release();

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    MTL::Buffer* pIndexBuffer       = nullptr;
    MTL::Buffer* pPositionBuffer    = nullptr;
    MTL::Buffer* pVertexColorBuffer = nullptr;
    CreateGeometryBuffers(renderer.get(), &pIndexBuffer, &pPositionBuffer, &pVertexColorBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "101_color_cube_metal");
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
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents()) {
        CA::MetalDrawable* pDrawable = renderer->Swapchain->nextDrawable();

        // nextDrawable() will return nil if there are no free swapchain buffers to render to
        if (pDrawable) {
            uint32_t swapchainIndex = (frameIndex % renderer->SwapchainBufferCount);

            MTL::RenderPassColorAttachmentDescriptor* pColorTargetDesc = MTL::RenderPassColorAttachmentDescriptor::alloc()->init();
            pColorTargetDesc->setClearColor(clearColor);
            pColorTargetDesc->setTexture(pDrawable->texture());
            pColorTargetDesc->setLoadAction(MTL::LoadActionClear);
            pColorTargetDesc->setStoreAction(MTL::StoreActionStore);
            pRenderPassDescriptor->colorAttachments()->setObject(pColorTargetDesc, 0);

            MTL::RenderPassDepthAttachmentDescriptor* pDepthTargetDesc = MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
            pDepthTargetDesc->setClearDepth(1);
            pDepthTargetDesc->setTexture(renderer->SwapchainDSVBuffers[swapchainIndex]);
            pDepthTargetDesc->setLoadAction(MTL::LoadActionClear);
            pDepthTargetDesc->setStoreAction(MTL::StoreActionDontCare);
            pRenderPassDescriptor->setDepthAttachment(pDepthTargetDesc);

            MTL::CommandBuffer*        pCommandBuffer = renderer->Queue->commandBuffer();
            MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);

            pRenderEncoder->setRenderPipelineState(pRenderPipelineState);
            pRenderEncoder->setDepthStencilState(pDepthStencilState);

            // Update the camera model view projection matrix
            mat4 modelMat = rotate(static_cast<float>(glfwGetTime()), vec3(0, 1, 0)) *
                            rotate(static_cast<float>(glfwGetTime()), vec3(1, 0, 0));
            mat4 viewMat = lookAt(vec3(0, 0, 2), vec3(0, 0, 0), vec3(0, 1, 0));
            mat4 projMat = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

            mat4 mvpMat = projMat * viewMat * modelMat;

            pRenderEncoder->setVertexBytes(&mvpMat, sizeof(glm::mat4), 2);

            MTL::Buffer* vbvs[2]    = {pPositionBuffer, pVertexColorBuffer};
            NS::UInteger offsets[2] = {0, 0};
            NS::Range    vbRange(0, 2);
            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

            pRenderEncoder->drawIndexedPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, 36, MTL::IndexTypeUInt32, pIndexBuffer, 0);

            pRenderEncoder->endEncoding();

            pCommandBuffer->presentDrawable(pDrawable);
            pCommandBuffer->commit();
        }
    }

    return 0;
}

void CreateGeometryBuffers(
    MetalRenderer* pRenderer,
    MTL::Buffer**  ppIndexBuffer,
    MTL::Buffer**  ppPositionBuffer,
    MTL::Buffer**  ppVertexColorBuffer)
{
    TriMesh::Options options;
    options.enableVertexColors = true;

    TriMesh mesh = TriMesh::Cube(vec3(1), false, options);

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        ppIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        ppPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetVertexColors()),
        DataPtr(mesh.GetVertexColors()),
        ppVertexColorBuffer));
}
