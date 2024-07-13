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

struct DrawParameters
{
    uint32_t    materialIndex = 0;
    uint32_t    numIndices    = 0;
    MetalBuffer indexBuffer;
};

struct Material
{
    glm::vec3 albedo = glm::vec3(1);
    uint32_t  _padding0;
    uint32_t  recieveLight = 1;
    uint32_t  _padding1[3];
};

// =============================================================================
// Shader code
// =============================================================================
const char* gShaders = R"(
#include <metal_stdlib>
using namespace metal;

struct Camera {
	float4x4 MVP;
	float3   LightPosition;
};

struct DrawParameters {
	uint MaterialIndex;
};

struct Material {
	float3 Albedo;
	uint   receiveLight;
};

struct VertexData {
	float3 PositionOS [[attribute(0)]];
	float3 Normal     [[attribute(1)]];
};

struct VSOutput {
	float4 PositionCS [[position]];
	float3 PositionOS;
	float3 Normal;
};

VSOutput vertex vertexMain(
			 VertexData vertexData [[stage_in]],
	constant Camera&    Camera     [[buffer(2)]])
{
	VSOutput output;
	output.PositionCS = Camera.MVP * float4(vertexData.PositionOS, 1.0f);
	output.PositionOS = vertexData.PositionOS;
	output.Normal = vertexData.Normal;
	return output;
}

float4 fragment fragmentMain( 
			 VSOutput        input      [[stage_in]],
	constant Camera&         Camera     [[buffer(1)]],
	constant DrawParameters& DrawParams [[buffer(2)]],
	constant Material*       Materials  [[buffer(3)]])
{
	float3 lightDir = normalize(Camera.LightPosition - input.PositionOS);
	float  diffuse = 0.7 * saturate(dot(lightDir, input.Normal));

	Material material = Materials[DrawParams.MaterialIndex];
	float3 color = material.Albedo;
	if (material.receiveLight) {
		color = (0.3 + diffuse) * material.Albedo;
	}

	return float4(color, 1);  
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

void CreateGeometryBuffers(
    MetalRenderer*               pRenderer,
    std::vector<DrawParameters>& outDrawParams,
    MetalBuffer*                 pMaterialBuffer,
    MetalBuffer*                 pPositionBuffer,
    MetalBuffer*                 pNormalBuffer,
    vec3*                        pLightPosition);

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
    MetalShader vsShader;
    MetalShader fsShader;
    NS::Error*  pError  = nullptr;
    auto        library = NS::TransferPtr(renderer->Device->newLibrary(
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
    MetalPipelineRenderState renderPipelineState;
    MetalDepthStencilState   depthStencilState;
    CHECK_CALL(CreateDrawNormalPipeline(
        renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &renderPipelineState,
        &depthStencilState));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    std::vector<DrawParameters> drawParams;
    MetalBuffer                 materialBuffer;
    MetalBuffer                 positionBuffer;
    MetalBuffer                 normalBuffer;
    vec3                        lightPosition;
    CreateGeometryBuffers(
        renderer.get(),
        drawParams,
        &materialBuffer,
        &positionBuffer,
        &normalBuffer,
        &lightPosition);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "102_cornell_box_metal");
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

        // Update the camera model view projection matrix
        mat4 modelMat = mat4(1);

        mat4 viewMat = lookAt(vec3(0, 3, 5), vec3(0, 2.8, 0), vec3(0, 1, 0));
        mat4 projMat = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
        mat4 mvpMat  = projMat * viewMat * modelMat;

        struct Camera
        {
            mat4   mvp;
            vec3   lightPosition;
            uint32 _padding;
        };

        Camera cam        = {};
        cam.mvp           = mvpMat;
        cam.lightPosition = lightPosition;

        pRenderEncoder->setVertexBytes(&cam, sizeof(Camera), 2);
        pRenderEncoder->setFragmentBytes(&cam, sizeof(Camera), 1);
        pRenderEncoder->setFragmentBuffer(materialBuffer.Buffer.get(), 0, 3);

        MTL::Buffer* vbvs[2]    = {positionBuffer.Buffer.get(), normalBuffer.Buffer.get()};
        NS::UInteger offsets[2] = {0, 0};
        NS::Range    vbRange(0, 2);
        pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

        for (auto& draw : drawParams) {
            pRenderEncoder->setFragmentBytes(&draw.materialIndex, sizeof(uint), 2);
            pRenderEncoder->drawIndexedPrimitives(
                MTL::PrimitiveType::PrimitiveTypeTriangle,
                draw.numIndices,
                MTL::IndexTypeUInt32,
                draw.indexBuffer.Buffer.get(),
                0);
        }

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateGeometryBuffers(
    MetalRenderer*               pRenderer,
    std::vector<DrawParameters>& outDrawParams,
    MetalBuffer*                 pMaterialBuffer,
    MetalBuffer*                 pPositionBuffer,
    MetalBuffer*                 pNormalBuffer,
    vec3*                        pLightPosition)
{
    TriMesh::Options options;
    options.enableVertexColors = true;
    options.enableNormals      = true;

    TriMesh mesh = TriMesh::CornellBox(options);

    uint32_t lightGroupIndex = mesh.GetGroupIndex("light");
    assert((lightGroupIndex != UINT32_MAX) && "group index for 'light' failed");

    *pLightPosition = mesh.GetGroup(lightGroupIndex).GetBounds().Center();

    std::vector<Material> materials;
    for (uint32_t materialIndex = 0; materialIndex < mesh.GetNumMaterials(); ++materialIndex) {
        auto& matDesc = mesh.GetMaterial(materialIndex);

        Material material     = {};
        material.albedo       = matDesc.baseColor;
        material.recieveLight = (matDesc.name != "white light") ? true : false;
        materials.push_back(material);

        auto triangles = mesh.GetTrianglesForMaterial(materialIndex);

        DrawParameters params = {};
        params.numIndices     = static_cast<uint32_t>(3 * triangles.size());
        params.materialIndex  = materialIndex;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(triangles),
            DataPtr(triangles),
            &params.indexBuffer));

        outDrawParams.push_back(params);
    }

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(materials),
        DataPtr(materials),
        pMaterialBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        pNormalBuffer));
}
