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
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngleX = 0.0f;
static float gAngleX       = 0.0f;
static float gTargetAngleY = 0.0f;
static float gAngleY       = 0.0f;

struct CameraProperties
{
    mat4     ModelMatrix;
    mat4     ViewProjectionMatrix;
    vec3     EyePosition;
    uint32_t _pad0;
};

void CreateTextures(
    MetalRenderer* pRenderer,
    MetalTexture*  pDiffuse,
    MetalTexture*  pDisplacement,
    MetalTexture*  pNorma);
void CreateGeometryBuffers(
    MetalRenderer* pRenderer,
    MetalBuffer*   pIndexBuffer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pTexCoordBuffer,
    MetalBuffer*   pNormalBuffer,
    MetalBuffer*   pTangentBuffer,
    MetalBuffer*   pBitangentBuffer);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    int dx = x - prevX;
    int dy = y - prevY;

    if (buttons & MOUSE_BUTTON_RIGHT)
    {
        gTargetAngleX += 0.25f * dy;
    }
    if (buttons & MOUSE_BUTTON_LEFT)
    {
        gTargetAngleY += 0.25f * dx;
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
    std::string shaderSource = LoadString("projects/306_parallax_occlusion_map/shaders.metal");

    MetalShader vsShader;
    MetalShader fsShader;
    NS::Error*  pError  = nullptr;
    auto        library = NS::TransferPtr(renderer->Device->newLibrary(
        NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding),
        nullptr,
        &pError));

    if (library.get() == nullptr)
    {
        std::stringstream ss;
        ss << "\n"
           << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
        return EXIT_FAILURE;
    }

    vsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
    if (vsShader.Function.get() == nullptr)
    {
        assert(false && "VS Shader MTL::Library::newFunction() failed");
        return EXIT_FAILURE;
    }

    fsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
    if (fsShader.Function.get() == nullptr)
    {
        assert(false && "FS Shader MTL::Library::newFunction() failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    MetalPipelineRenderState renderPipelineState;
    MetalDepthStencilState   depthStencilState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        &vsShader,
        &fsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &renderPipelineState,
        &depthStencilState));

    // *************************************************************************
    // Texture
    // *************************************************************************
    MetalTexture diffuseTexture;
    MetalTexture dispTexture;
    MetalTexture normalTexture;
    CreateTextures(renderer.get(), &diffuseTexture, &dispTexture, &normalTexture);

    // *************************************************************************
    // Geometry data
    // *************************************************************************
    MetalBuffer indexBuffer;
    uint32_t    numIndices;
    MetalBuffer positionBuffer;
    MetalBuffer texCoordBuffer;
    MetalBuffer normalBuffer;
    MetalBuffer tangentBuffer;
    MetalBuffer bitangentBuffer;
    CreateGeometryBuffers(
        renderer.get(),
        &indexBuffer,
        &numIndices,
        &positionBuffer,
        &texCoordBuffer,
        &normalBuffer,
        &tangentBuffer,
        &bitangentBuffer);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "306_parallax_occlusion_map_metal");
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
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents())
    {
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

        // Smooth out the rotation
        gAngleX += (gTargetAngleX - gAngleX) * 0.1f;
        gAngleY += (gTargetAngleY - gAngleY) * 0.1f;

        mat4 modelMat = glm::rotate(glm::radians(gAngleY), vec3(0, 1, 0)) *
                        glm::rotate(glm::radians(gAngleX), vec3(1, 0, 0));

        vec3 eyePos      = vec3(0, 1.0f, 1.25f);
        mat4 viewMat     = lookAt(eyePos, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat     = perspective(radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
        mat4 projViewMat = projMat * viewMat;

        CameraProperties cam     = {};
        cam.ModelMatrix          = modelMat;
        cam.ViewProjectionMatrix = projViewMat;
        cam.EyePosition          = eyePos;

        pRenderEncoder->setVertexBytes(&cam, sizeof(CameraProperties), 5);
        pRenderEncoder->setFragmentBytes(&cam, sizeof(CameraProperties), 5);
        pRenderEncoder->setFragmentTexture(diffuseTexture.Texture.get(), 0);
        pRenderEncoder->setFragmentTexture(normalTexture.Texture.get(), 1);
        pRenderEncoder->setFragmentTexture(dispTexture.Texture.get(), 2);

        MTL::Buffer* vbvs[5] = {
            positionBuffer.Buffer.get(),
            texCoordBuffer.Buffer.get(),
            normalBuffer.Buffer.get(),
            tangentBuffer.Buffer.get(),
            bitangentBuffer.Buffer.get()};

        NS::UInteger offsets[5] = {0, 0, 0};
        NS::Range    vbRange(0, 5);
        pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

        pRenderEncoder->drawIndexedPrimitives(
            MTL::PrimitiveType::PrimitiveTypeTriangle,
            numIndices,
            MTL::IndexTypeUInt32,
            indexBuffer.Buffer.get(),
            0);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateTextures(
    MetalRenderer* pRenderer,
    MetalTexture*  pDiffuse,
    MetalTexture*  pDisplacement,
    MetalTexture*  pNormal)
{
    auto dir = GetAssetPath("textures/red_brick_03");

    // Diffuse
    {
        auto mipmap = MipmapT(
            LoadImage8u(dir / "diffuse.png"),
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_FILTER_MODE_LINEAR);
        assert((mipmap.GetSizeInBytes() > 0) && "diffuse image load failed");

        CHECK_CALL(CreateTexture(
            pRenderer,
            mipmap.GetWidth(0),
            mipmap.GetHeight(0),
            MTL::PixelFormatRGBA8Unorm,
            mipmap.GetMipOffsets(),
            mipmap.GetSizeInBytes(),
            mipmap.GetPixels(),
            pDiffuse));
    }

    // Normal
    {
        auto mipmap = MipmapT(
            LoadImage8u(dir / "normal_dx.png"),
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_SAMPLE_MODE_CLAMP,
            BITMAP_FILTER_MODE_LINEAR);
        assert((mipmap.GetSizeInBytes() > 0) && "normal image load failed");

        CHECK_CALL(CreateTexture(
            pRenderer,
            mipmap.GetWidth(0),
            mipmap.GetHeight(0),
            MTL::PixelFormatRGBA8Unorm,
            mipmap.GetMipOffsets(),
            mipmap.GetSizeInBytes(),
            mipmap.GetPixels(),
            pNormal));
    }

    // Displacement
    {
        auto bitmap = LoadImage8u(dir / "disp.png");
        assert((bitmap.GetSizeInBytes() > 0) && "disp image load failed");

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            MTL::PixelFormatRGBA8Unorm,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pDisplacement));
    }
}

void CreateGeometryBuffers(
    MetalRenderer* pRenderer,
    MetalBuffer*   pIndexBuffer,
    uint32_t*      pNumIndices,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pTexCoordBuffer,
    MetalBuffer*   pNormalBuffer,
    MetalBuffer*   pTangentBuffer,
    MetalBuffer*   pBitangentBuffer)
{
    TriMesh::Options options;
    options.enableTexCoords = true;
    options.enableNormals   = true;
    options.enableTangents  = true;
    TriMesh mesh            = TriMesh::Cube(vec3(1), false, options);

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        pIndexBuffer));

    *pNumIndices = mesh.GetNumIndices();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        pTexCoordBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        pNormalBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTangents()),
        DataPtr(mesh.GetTangents()),
        pTangentBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetBitangents()),
        DataPtr(mesh.GetBitangents()),
        pBitangentBuffer));
}
