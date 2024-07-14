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
// Constants
// =============================================================================

const std::vector<std::string> gModelNames = {
    "Sphere (Generated)",
    "Cone",
    "Teapot",
    "Knob",
    "Sphere (OBJ)",
    "Torus",
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

struct Geometry
{
    uint32_t    numIndices;
    MetalBuffer indexBuffer;
    MetalBuffer positionBuffer;
    MetalBuffer vertexColorBuffer;
    uint32_t    tbnDebugNumVertices;
    MetalBuffer tbnDebugVertexBuffer;
};

void CreateGeometryBuffers(
    MetalRenderer*         pRenderer,
    std::vector<Geometry>& outGeometries);

static uint32_t gModelIndex = 0;

static int   sPrevX;
static int   sPrevY;
static float sAngleX       = 0;
static float sAngleY       = 0;
static float sTargetAngleX = 0;
static float sTargetAngleY = 0;

void MouseDown(int x, int y, int buttons)
{
    if (buttons & MOUSE_BUTTON_LEFT) {
        sPrevX = x;
        sPrevY = y;
    }
}

void MouseMove(int x, int y, int buttons)
{
    if (buttons & MOUSE_BUTTON_LEFT) {
        int dx = x - sPrevX;
        int dy = y - sPrevY;

        sTargetAngleX += 0.25f * dy;
        sTargetAngleY += 0.25f * dx;

        sPrevX = x;
        sPrevY = y;
    }
}

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
    MetalPipelineRenderState trianglePipelineState;
    MetalDepthStencilState   triangleDepthStencilState;
    CHECK_CALL(CreateDrawVertexColorPipeline(
        renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &trianglePipelineState,
        &triangleDepthStencilState));

    MetalPipelineRenderState tbnDebugPipelineState;
    MetalDepthStencilState   tbnDebugDepthStencilState;
    CHECK_CALL(CreateDrawVertexColorPipeline(
        renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &tbnDebugPipelineState,
        &tbnDebugDepthStencilState,
        MTL::PrimitiveTopologyClassLine,
        METAL_PIPELINE_FLAGS_INTERLEAVED_ATTRS));

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    std::vector<Geometry> geometries;
    CreateGeometryBuffers(
        renderer.get(),
        geometries);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "104_debug_tbn_metal");
    if (!window) {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    window->AddMouseDownCallbacks(MouseDown);
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Render Pass Description
    // *************************************************************************
    MTL::RenderPassDescriptor* pRenderPassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT)) {
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
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    ImGuiIO& io                = ImGui::GetIO();
    io.DisplayFramebufferScale = ImVec2(1, 1);

    while (window->PollEvents()) {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

        if (ImGui::Begin("Scene")) {
            static const char* currentModelNames = gModelNames[gModelIndex].c_str();

            if (ImGui::BeginCombo("Model", currentModelNames)) {
                for (size_t i = 0; i < gModelNames.size(); ++i) {
                    bool isSelected = (currentModelNames == gModelNames[i]);
                    if (ImGui::Selectable(gModelNames[i].c_str(), isSelected)) {
                        currentModelNames = gModelNames[i].c_str();
                        gModelIndex       = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::End();

        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

        uint32_t swapchainIndex = (frameIndex % renderer->SwapchainBufferCount);
        frameIndex++;

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

        pRenderEncoder->setRenderPipelineState(trianglePipelineState.State.get());
        pRenderEncoder->setDepthStencilState(triangleDepthStencilState.State.get());

        mat4 modelMat = glm::rotate(glm::radians(sAngleX), vec3(1, 0, 0)) * glm::rotate(glm::radians(sAngleY), vec3(0, 1, 0));
        mat4 viewMat  = lookAt(vec3(0, 1, 2), vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat  = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
        mat4 mvpMat   = projMat * viewMat * modelMat;

        sAngleX += (sTargetAngleX - sAngleX) * 0.1f;
        sAngleY += (sTargetAngleY - sAngleY) * 0.1f;

        pRenderEncoder->setVertexBytes(&mvpMat, sizeof(glm::mat4), 2);

        auto& geo = geometries[gModelIndex];

        MTL::Buffer* vbvs[2]    = {geo.positionBuffer.Buffer.get(), geo.vertexColorBuffer.Buffer.get()};
        NS::UInteger offsets[2] = {0, 0};
        NS::Range    vbRange(0, 2);
        pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

        pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
        pRenderEncoder->setCullMode(MTL::CullModeBack);

        pRenderEncoder->drawIndexedPrimitives(
            MTL::PrimitiveType::PrimitiveTypeTriangle,
            geo.numIndices,
            MTL::IndexTypeUInt32,
            geo.indexBuffer.Buffer.get(),
            0);

        // TBN debug
        {
            pRenderEncoder->setRenderPipelineState(tbnDebugPipelineState.State.get());
            pRenderEncoder->setDepthStencilState(tbnDebugDepthStencilState.State.get());

            pRenderEncoder->setVertexBuffer(geo.tbnDebugVertexBuffer.Buffer.get(), 0, 0);

            pRenderEncoder->setCullMode(MTL::CullModeNone);

            pRenderEncoder->drawPrimitives(MTL::PrimitiveTypeLine, 0, geo.tbnDebugNumVertices, 1);
        }

        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateGeometryBuffers(
    MetalRenderer*         pRenderer,
    std::vector<Geometry>& outGeometries)
{
    TriMesh::Options options;
    options.enableVertexColors = true;
    options.enableTexCoords    = true;
    options.enableNormals      = true;
    options.enableTangents     = true;

    std::vector<TriMesh> meshes;
    meshes.push_back(TriMesh::Sphere(1.0f, 16, 16, options));
    meshes.push_back(TriMesh::Cone(1, 1, 32, options));

    // Teapot
    {
        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/teapot.obj").string(), "", options, &mesh);
        assert(res && "OBJ load failed");
        mesh.ScaleToFit();
        meshes.push_back(mesh);
    }

    // Knob
    {
        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh);
        assert(res && "OBJ load failed");
        mesh.ScaleToFit();
        meshes.push_back(mesh);
    }

    // Sphere
    {
        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/sphere.obj").string(), "", options, &mesh);
        assert(res && "OBJ load failed");
        mesh.ScaleToFit();
        meshes.push_back(mesh);
    }

    // Torus
    {
        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/torus.obj").string(), "", options, &mesh);
        assert(res && "OBJ load failed");
        mesh.ScaleToFit();
        meshes.push_back(mesh);
    }

    for (uint32_t i = 0; i < meshes.size(); ++i) {
        auto& mesh = meshes[i];

        Geometry geo = {};

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetVertexColors()),
            DataPtr(mesh.GetVertexColors()),
            &geo.vertexColorBuffer));

        geo.numIndices = 3 * mesh.GetNumTriangles();

        auto tbnVertexData = mesh.GetTBNLineSegments(&geo.tbnDebugNumVertices);
        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(tbnVertexData),
            DataPtr(tbnVertexData),
            &geo.tbnDebugVertexBuffer));

        outGeometries.push_back(geo);
    }
}
