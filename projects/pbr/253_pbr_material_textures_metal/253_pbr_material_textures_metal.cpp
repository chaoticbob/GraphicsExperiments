#include "window.h"

#include "mtl_renderer.h"
#include "bitmap.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <fstream>

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

#define MATERIAL_TEXTURE_STRIDE 4
#define NUM_MATERIALS           16
#define TOTAL_MATERIAL_TEXTURES (NUM_MATERIALS * MATERIAL_TEXTURE_STRIDE)

#define MAX_IBLS                                 32
#define IBL_INTEGRATION_LUT_DESCRIPTOR_OFFSET    3
#define IBL_INTEGRATION_MS_LUT_DESCRIPTOR_OFFSET 4
#define IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET    16
#define IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET   (IBL_IRRADIANCE_MAPS_DESCRIPTOR_OFFSET + MAX_IBLS)
#define MATERIAL_TEXTURES_DESCRIPTOR_OFFSET      (IBL_ENVIRONMENT_MAPS_DESCRIPTOR_OFFSET + MAX_IBLS)

// This will be passed in via constant buffer
struct Light
{
    uint32_t active;
    uint32_t __pad0[3];
    vec3     position;
    uint32_t __pad1;
    vec3     color;
    uint32_t __pad2;
    float    intensity;
    uint32_t __pad3[3];
};

struct SceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t __pad0;
    uint32_t numLights;
    uint32_t __pad1[3];
    Light    lights[8];
    uint32_t iblNumEnvLevels;
    uint32_t iblIndex;
    uint     multiscatter;
    uint     colorCorrect;
};

struct MaterialParameters
{
    float    specular;
    uint32_t __pad0[3];
};

struct DrawParameters
{
    mat4     ModelMatrix;
    uint32_t MaterialIndex;
    uint32_t InvertNormalMapY;
    uint32_t __pad0[2];
};

struct MaterialTextures
{
    MetalTexture baseColorTexture;
    MetalTexture normalTexture;
    MetalTexture roughnessTexture;
    MetalTexture metallicTexture;
};

struct GeometryBuffers
{
    uint32_t    numIndices;
    MetalBuffer indexBuffer;
    MetalBuffer positionBuffer;
    MetalBuffer texCoordBuffer;
    MetalBuffer normalBuffer;
    MetalBuffer tangentBuffer;
    MetalBuffer bitangentBuffer;
};

// =============================================================================
// Constants
// =============================================================================

const std::vector<std::string> gModelNames = {
    "Sphere",
    "Knob",
    "Monkey",
    "Cube",
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static std::vector<std::string> gMaterialNames = {};

static uint32_t                 gNumLights  = 4;
static const uint32_t           gMaxIBLs    = MAX_IBLS;
static uint32_t                 gIBLIndex   = 0;
static std::vector<std::string> gIBLNames   = {};
static uint32_t                 gModelIndex = 0;

void CreateEnvironmentVertexBuffers(
    MetalRenderer*   pRenderer,
    GeometryBuffers& outGeomtryBuffers);
void CreateMaterialModels(
    MetalRenderer*                pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers);
void CreateIBLTextures(
    MetalRenderer*             pRenderer,
    MetalTexture*              pBRDFLUT,
    MetalTexture*              pMultiscatterBRDFLUT,
    std::vector<MetalTexture>& outIrradianceTextures,
    std::vector<MetalTexture>& outEnvironmentTextures,
    std::vector<uint32_t>&     outEnvNumLevels);
void CreateMaterials(
    MetalRenderer*                   pRenderer,
    MaterialTextures&                outDefaultMaterialTextures,
    std::vector<MaterialTextures>&   outMaterialTexturesSets,
    std::vector<MaterialParameters>& outMaterialParametersSets);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT) {
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

    if (!InitMetal(renderer.get(), gEnableDebug)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    // PBR shaders
    MetalShader pbrVsShader;
    MetalShader pbrFsShader;
    NS::Error*  pError = nullptr;
    {
        std::string shaderSource = LoadString("projects/253_pbr_material_textures/shaders.metal");
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
               << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        pbrVsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
        if (pbrVsShader.Function.get() == nullptr) {
            assert(false && "VS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        pbrFsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
        if (pbrFsShader.Function.get() == nullptr) {
            assert(false && "FS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }
    }

    // Draw texture shaders
    MetalShader drawTextureVsShader;
    MetalShader drawTextureFsShader;
    {
        std::string shaderSource = LoadString("projects/253_pbr_material_textures/drawtexture.metal");
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
               << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        drawTextureVsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
        if (drawTextureVsShader.Function.get() == nullptr) {
            assert(false && "VS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }

        drawTextureFsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
        if (drawTextureFsShader.Function.get() == nullptr) {
            assert(false && "FS Shader MTL::Library::newFunction() failed");
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // PBR pipeline state object
    // *************************************************************************
    MetalPipelineRenderState pbrPipelineState;
    MetalDepthStencilState   pbrDepthStencilState;
    CHECK_CALL(CreateGraphicsPipeline1(
        renderer.get(),
        &pbrVsShader,
        &pbrFsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &pbrPipelineState,
        &pbrDepthStencilState));

    // *************************************************************************
    // Environment pipeline state object
    // *************************************************************************
    MetalPipelineRenderState envPipelineState;
    MetalDepthStencilState   envDepthStencilState;
    CHECK_CALL(CreateDrawTexturePipeline(
        renderer.get(),
        &drawTextureVsShader,
        &drawTextureFsShader,
        GREX_DEFAULT_RTV_FORMAT,
        GREX_DEFAULT_DSV_FORMAT,
        &envPipelineState,
        &envDepthStencilState));

    // *************************************************************************
    // Environment vertex buffers
    // *************************************************************************
    GeometryBuffers envGeoBuffers;
    CreateEnvironmentVertexBuffers(
        renderer.get(),
        envGeoBuffers);

    // *************************************************************************
    // Material models
    // *************************************************************************
    std::vector<GeometryBuffers> matGeoBuffers;
    CreateMaterialModels(
        renderer.get(),
        matGeoBuffers);

    // *************************************************************************
    // Environment texture
    // *************************************************************************
    MetalTexture              brdfLUT;
    MetalTexture              multiscatterBRDFLUT;
    std::vector<MetalTexture> irrTextures;
    std::vector<MetalTexture> envTextures;
    std::vector<uint32_t>     envNumLevels;
    CreateIBLTextures(renderer.get(), &brdfLUT, &multiscatterBRDFLUT, irrTextures, envTextures, envNumLevels);

    // *************************************************************************
    // Material texture
    // *************************************************************************
    MaterialTextures                defaultMaterialTextures;
    std::vector<MaterialTextures>   materialTexturesSets;
    std::vector<MaterialParameters> materialParametersSets;
    CreateMaterials(
        renderer.get(),
        defaultMaterialTextures,
        materialTexturesSets,
        materialParametersSets);

    // *************************************************************************
    // Material buffer
    // *************************************************************************
    MetalBuffer materialBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        SizeInBytes(materialParametersSets),
        DataPtr(materialParametersSets),
        &materialBuffer));

    // *************************************************************************
    // Texture Arrays
    // *************************************************************************
    MTL::Buffer*               pbrIBLTexturesArgBuffer = nullptr;
    std::vector<MTL::Texture*> iblEnvTextures;

    {
        MTL::ArgumentEncoder* pbrIBLTexturesArgEncoder = pbrFsShader.Function->newArgumentEncoder(5);

        pbrIBLTexturesArgBuffer = renderer->Device->newBuffer(pbrIBLTexturesArgEncoder->encodedLength(), MTL::ResourceStorageModeManaged);

        pbrIBLTexturesArgEncoder->setArgumentBuffer(pbrIBLTexturesArgBuffer, 0);

        pbrIBLTexturesArgEncoder->setTexture(brdfLUT.Texture.get(), 0);
        pbrIBLTexturesArgEncoder->setTexture(multiscatterBRDFLUT.Texture.get(), 1);

        // Irradiance
        for (size_t i = 0; i < irrTextures.size(); ++i) {
            pbrIBLTexturesArgEncoder->setTexture(irrTextures[i].Texture.get(), 2 + i);
        }

        // Environment
        for (size_t i = 0; i < envTextures.size(); ++i) {
            iblEnvTextures.push_back(envTextures[i].Texture.get());
            pbrIBLTexturesArgEncoder->setTexture(envTextures[i].Texture.get(), 2 + MAX_IBLS + i);
        }

        pbrIBLTexturesArgBuffer->didModifyRange(NS::Range::Make(0, pbrIBLTexturesArgBuffer->length()));

        pbrIBLTexturesArgEncoder->release();
    }

    // Materials
    MTL::Buffer* pbrEnvMaterialTexturesArgBuffer = nullptr;

    {
        MTL::ArgumentEncoder* pbrEnvMaterialTexturesArgEncoder = pbrFsShader.Function->newArgumentEncoder(6);

        pbrEnvMaterialTexturesArgBuffer = renderer->Device->newBuffer(pbrEnvMaterialTexturesArgEncoder->encodedLength(), MTL::ResourceStorageModeManaged);

        pbrEnvMaterialTexturesArgEncoder->setArgumentBuffer(pbrEnvMaterialTexturesArgBuffer, 0);

        uint32_t argBufferIndex = 0;
        for (size_t i = 0; i < materialTexturesSets.size(); ++i) {
            const MaterialTextures* currentMaterial = &materialTexturesSets[i];
            pbrEnvMaterialTexturesArgEncoder->setTexture(currentMaterial->baseColorTexture.Texture.get(), argBufferIndex++);
            pbrEnvMaterialTexturesArgEncoder->setTexture(currentMaterial->normalTexture.Texture.get(), argBufferIndex++);
            pbrEnvMaterialTexturesArgEncoder->setTexture(currentMaterial->roughnessTexture.Texture.get(), argBufferIndex++);
            pbrEnvMaterialTexturesArgEncoder->setTexture(currentMaterial->metallicTexture.Texture.get(), argBufferIndex++);
        }

        pbrEnvMaterialTexturesArgBuffer->didModifyRange(NS::Range::Make(0, pbrEnvMaterialTexturesArgBuffer->length()));

        pbrEnvMaterialTexturesArgEncoder->release();
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "203_pbr_align_metal");
    if (!window) {
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
    // Persistent map parameters
    // *************************************************************************
    SceneParameters sceneParams = {};

    // *************************************************************************
    // Set some scene params
    // *************************************************************************
    sceneParams.numLights           = gNumLights;
    sceneParams.lights[0].active    = 0;
    sceneParams.lights[0].position  = vec3(3, 10, 0);
    sceneParams.lights[0].color     = vec3(1, 1, 1);
    sceneParams.lights[0].intensity = 1.5f;
    sceneParams.lights[1].active    = 0;
    sceneParams.lights[1].position  = vec3(-8, 1, 4);
    sceneParams.lights[1].color     = vec3(0.85f, 0.95f, 0.81f);
    sceneParams.lights[1].intensity = 0.4f;
    sceneParams.lights[2].active    = 0;
    sceneParams.lights[2].position  = vec3(0, 8, -8);
    sceneParams.lights[2].color     = vec3(0.89f, 0.89f, 0.97f);
    sceneParams.lights[2].intensity = 0.95f;
    sceneParams.lights[3].active    = 0;
    sceneParams.lights[3].position  = vec3(15, 0, 0);
    sceneParams.lights[3].color     = vec3(0.92f, 0.5f, 0.7f);
    sceneParams.lights[3].intensity = 0.5f;
    sceneParams.iblNumEnvLevels     = envNumLevels[gIBLIndex];
    sceneParams.iblIndex            = gIBLIndex;
    sceneParams.colorCorrect        = 0;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents()) {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

        if (ImGui::Begin("Scene")) {
            static const char* currentModelName = gModelNames[0].c_str();
            if (ImGui::BeginCombo("Model", currentModelName)) {
                for (size_t i = 0; i < gModelNames.size(); ++i) {
                    bool isSelected = (currentModelName == gModelNames[i]);
                    if (ImGui::Selectable(gModelNames[i].c_str(), isSelected)) {
                        currentModelName = gModelNames[i].c_str();
                        gModelIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName)) {
                for (size_t i = 0; i < gIBLNames.size(); ++i) {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected)) {
                        currentIBLName       = gIBLNames[i].c_str();
                        sceneParams.iblIndex = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            ImGui::Checkbox("Multiscatter", reinterpret_cast<bool*>(&sceneParams.multiscatter));

            ImGui::Separator();

            ImGui::Checkbox("Color Correct", reinterpret_cast<bool*>(&sceneParams.colorCorrect));

            ImGui::Separator();

            for (uint32_t lightIdx = 0; lightIdx < 4; ++lightIdx) {
                std::stringstream lightName;
                lightName << "Light " << lightIdx;
                if (ImGui::TreeNodeEx(lightName.str().c_str(), ImGuiTreeNodeFlags_None)) {
                    ImGui::Checkbox("Active", reinterpret_cast<bool*>(&sceneParams.lights[lightIdx].active));
                    ImGui::SliderFloat("Intensity", &sceneParams.lights[lightIdx].intensity, 0.0f, 10.0f);
                    ImGui::ColorPicker3("Albedo", reinterpret_cast<float*>(&(sceneParams.lights[lightIdx].color)), ImGuiColorEditFlags_NoInputs);

                    ImGui::TreePop();
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("Material Parameters")) {
            for (uint32_t matIdx = 0; matIdx < gMaterialNames.size(); ++matIdx) {
                if (ImGui::TreeNodeEx(gMaterialNames[matIdx].c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("Specular", &(materialParametersSets[matIdx].specular), 0.0f, 1.0f);

                    ImGui::TreePop();
                }

                ImGui::Separator();
            }
        }
        ImGui::End();

        // ---------------------------------------------------------------------

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

        MTL::Viewport viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
        pRenderEncoder->setViewport(viewport);
        MTL::ScissorRect scissor = {0, 0, gWindowWidth, gWindowHeight};
        pRenderEncoder->setScissorRect(scissor);

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.1f;

        // Camera matrices
        mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
        vec3 startingEyePosition = vec3(0, 2.5f, 10);
        vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
        mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set scene params values that required calculation
        sceneParams.viewProjectionMatrix = projMat * viewMat;
        sceneParams.eyePosition          = eyePosition;
        sceneParams.iblNumEnvLevels      = envNumLevels[gIBLIndex];

        // Draw environment
        {
            pRenderEncoder->setRenderPipelineState(envPipelineState.State.get());
            pRenderEncoder->setDepthStencilState(envDepthStencilState.State.get());

            glm::mat4 moveUp = glm::translate(vec3(0, 5, 0));

            // SceneParams [[buffer(2)]]
            struct
            {
                mat4     MVP;
                uint32_t iblIndex;
                uint32_t _pad0[3];
            } envSceneParams;

            envSceneParams.MVP      = projMat * viewMat * moveUp;
            envSceneParams.iblIndex = sceneParams.iblIndex;

            pRenderEncoder->setVertexBytes(&envSceneParams, sizeof(envSceneParams), 2);
            pRenderEncoder->setFragmentBytes(&envSceneParams, sizeof(envSceneParams), 2);

            // Textures
            pRenderEncoder->setFragmentTextures(DataPtr(iblEnvTextures), NS::Range(0, gMaxIBLs));

            // Vertex buffers
            MTL::Buffer* vbvs[2]    = {};
            NS::UInteger offsets[2] = {};
            NS::Range    vbRange(0, 2);
            // Position
            vbvs[0]    = envGeoBuffers.positionBuffer.Buffer.get();
            offsets[0] = 0;
            // Tex coord
            vbvs[1]    = envGeoBuffers.texCoordBuffer.Buffer.get();
            offsets[1] = 0;
            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeFront);

            pRenderEncoder->drawIndexedPrimitives(
                MTL::PrimitiveType::PrimitiveTypeTriangle,
                envGeoBuffers.numIndices,
                MTL::IndexTypeUInt32,
                envGeoBuffers.indexBuffer.Buffer.get(),
                0);
        }

        // Draw sample spheres
        {
            // SceneParams [[buffer(6/3)]]
            pRenderEncoder->setVertexBytes(&sceneParams, sizeof(SceneParameters), 6);
            pRenderEncoder->setFragmentBytes(&sceneParams, sizeof(SceneParameters), 3);
            // MaterialParameters [[buffer(4)]]
            pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), sizeof(MaterialParameters) * CountU32(materialParametersSets), 4);
            // Textures
            pRenderEncoder->setFragmentBuffer(pbrIBLTexturesArgBuffer, 0, 5);
            pRenderEncoder->setFragmentBuffer(pbrEnvMaterialTexturesArgBuffer, 0, 6);

            // Setup ArgBuffers Usages
            {
                pRenderEncoder->useResource(brdfLUT.Texture.get(), MTL::ResourceUsageRead);
                pRenderEncoder->useResource(brdfLUT.Texture.get(), MTL::ResourceUsageRead);

                uint32_t argBufferIndex = 0;
                pRenderEncoder->useResource(brdfLUT.Texture.get(), MTL::ResourceUsageRead);
                pRenderEncoder->useResource(multiscatterBRDFLUT.Texture.get(), MTL::ResourceUsageRead);

                // Irradiance
                for (size_t i = 0; i < irrTextures.size(); ++i) {
                    pRenderEncoder->useResource(irrTextures[i].Texture.get(), MTL::ResourceUsageRead);
                }

                // Environment
                for (size_t i = 0; i < envTextures.size(); ++i) {
                    pRenderEncoder->useResource(envTextures[i].Texture.get(), MTL::ResourceUsageRead);
                }

                for (size_t i = 0; i < materialTexturesSets.size(); ++i) {
                    const MaterialTextures* currentMaterial = &materialTexturesSets[i];
                    pRenderEncoder->useResource(currentMaterial->baseColorTexture.Texture.get(), MTL::ResourceUsageRead);
                    pRenderEncoder->useResource(currentMaterial->normalTexture.Texture.get(), MTL::ResourceUsageRead);
                    pRenderEncoder->useResource(currentMaterial->roughnessTexture.Texture.get(), MTL::ResourceUsageRead);
                    pRenderEncoder->useResource(currentMaterial->metallicTexture.Texture.get(), MTL::ResourceUsageRead);
                }
            }

            pRenderEncoder->useResource(brdfLUT.Texture.get(), MTL::ResourceUsageRead);

            // Select which model to draw
            const GeometryBuffers& geoBuffers = matGeoBuffers[gModelIndex];

            // Vertex buffers
            MTL::Buffer* vbvs[5]    = {};
            NS::UInteger offsets[5] = {};
            NS::Range    vbRange(0, 5);
            // Position
            vbvs[0]    = geoBuffers.positionBuffer.Buffer.get();
            offsets[0] = 0;
            // Tex coord
            vbvs[1]    = geoBuffers.texCoordBuffer.Buffer.get();
            offsets[1] = 0;
            // Normal
            vbvs[2]    = geoBuffers.normalBuffer.Buffer.get();
            offsets[2] = 0;
            // Tangent
            vbvs[3]    = geoBuffers.tangentBuffer.Buffer.get();
            offsets[3] = 0;
            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);
            // Bitangent
            vbvs[4]    = geoBuffers.bitangentBuffer.Buffer.get();
            offsets[4] = 0;
            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeFront);

            // Pipeline state
            pRenderEncoder->setRenderPipelineState(pbrPipelineState.State.get());
            pRenderEncoder->setDepthStencilState(pbrDepthStencilState.State.get());

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeBack);

            const float yPos             = 0.0f;
            uint32_t    materialIndex    = 0;
            uint32_t    invertNormalMapY = false; // Invert if sphere

            // Material 0
            {
                glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, 4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 1
            {
                glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, 4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 2
            {
                glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, 4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 3
            {
                glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, 4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 4
            {
                glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, 1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 5
            {
                glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, 1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 6
            {
                glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, 1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 7
            {
                glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, 1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 8
            {
                glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, -1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 9
            {
                glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, -1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 10
            {
                glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, -1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 11
            {
                glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, -1.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 12
            {
                glm::mat4 modelMat = glm::translate(vec3(-4.5f, yPos, -4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 13
            {
                glm::mat4 modelMat = glm::translate(vec3(-1.5f, yPos, -4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 14
            {
                glm::mat4 modelMat = glm::translate(vec3(1.5f, yPos, -4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }

            // Material 15
            {
                glm::mat4 modelMat = glm::translate(vec3(4.5f, yPos, -4.5f));

                DrawParameters drawParams   = {};
                drawParams.ModelMatrix      = modelMat;
                drawParams.MaterialIndex    = materialIndex;
                drawParams.InvertNormalMapY = invertNormalMapY;

                // DrawParams [[buffers(5/2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 5);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(materialParametersSets), CountU32(materialParametersSets) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);

                if (materialIndex < (materialTexturesSets.size() - 1)) {
                    ++materialIndex;
                }
            }
        }

        // Draw ImGui
        window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);

        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateEnvironmentVertexBuffers(
    MetalRenderer*   pRenderer,
    GeometryBuffers& outGeomtryBuffers)
{
    TriMesh::Options options = {};
    options.enableTexCoords  = true;
    options.faceInside       = true;

    TriMesh mesh = TriMesh::Sphere(25, 64, 64, options);

    outGeomtryBuffers.numIndices = 3 * mesh.GetNumTriangles();

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        &outGeomtryBuffers.indexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        &outGeomtryBuffers.positionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTexCoords()),
        DataPtr(mesh.GetTexCoords()),
        &outGeomtryBuffers.texCoordBuffer));
}

void CreateMaterialModels(
    MetalRenderer*                pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers)
{
    // Sphere
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;

        TriMesh mesh = TriMesh::Sphere(1, 256, 256, options);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Knob
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.invertTexCoordsV = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh)) {
            return;
        }
        mesh.ScaleToFit(1.0f);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Monkey
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(180.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/monkey.obj").string(), "", options, &mesh)) {
            return;
        }
        // mesh.ScaleToUnit();

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Cube
    {
        TriMesh::Options options = {};
        options.enableTexCoords  = true;
        options.enableNormals    = true;
        options.enableTangents   = true;
        options.applyTransform   = true;

        TriMesh mesh = TriMesh::Cube(vec3(2), false, options);

        GeometryBuffers buffers = {};

        buffers.numIndices = 3 * mesh.GetNumTriangles();

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &buffers.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &buffers.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTexCoords()),
            DataPtr(mesh.GetTexCoords()),
            &buffers.texCoordBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTangents()),
            DataPtr(mesh.GetTangents()),
            &buffers.tangentBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetBitangents()),
            DataPtr(mesh.GetBitangents()),
            &buffers.bitangentBuffer));

        outGeomtryBuffers.push_back(buffers);
    }
}

void CreateIBLTextures(
    MetalRenderer*             pRenderer,
    MetalTexture*              pBRDFLUT,
    MetalTexture*              pMultiscatterBRDFLUT,
    std::vector<MetalTexture>& outIrradianceTextures,
    std::vector<MetalTexture>& outEnvironmentTextures,
    std::vector<uint32_t>&     outEnvNumLevels)
{
    // BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
        if (bitmap.Empty()) {
            assert(false && "Load image failed");
            return;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            MTL::PixelFormatRGBA32Float,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pBRDFLUT));
    }

    // Multiscatter BRDF LUT
    {
        auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut_ms.hdr"));
        if (bitmap.Empty()) {
            assert(false && "Load image failed");
            return;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            bitmap.GetWidth(),
            bitmap.GetHeight(),
            MTL::PixelFormatRGBA32Float,
            bitmap.GetSizeInBytes(),
            bitmap.GetPixels(),
            pMultiscatterBRDFLUT));
    }

    auto                               iblDir = GetAssetPath("IBL");
    std::vector<std::filesystem::path> iblFiles;
    for (auto& entry : std::filesystem::directory_iterator(iblDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto path = entry.path();
        auto ext  = path.extension();
        if (ext == ".ibl") {
            path = std::filesystem::relative(path, iblDir.parent_path());
            iblFiles.push_back(path);
        }
    }

    // Sort the paths so we match functionality on Windows
    std::sort(iblFiles.begin(), iblFiles.end());

    size_t maxEntries = std::min<size_t>(gMaxIBLs, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i) {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl)) {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            assert(false && "IBL maps load failed failed");
            return;
        }

        outEnvNumLevels.push_back(ibl.numLevels);

        // Irradiance
        {
            MetalTexture texture = {};
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.irradianceMap.GetWidth(),
                ibl.irradianceMap.GetHeight(),
                MTL::PixelFormatRGBA32Float,
                ibl.irradianceMap.GetSizeInBytes(),
                ibl.irradianceMap.GetPixels(),
                &texture));
            outIrradianceTextures.push_back(texture);
        }

        // Environment
        {
            const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
            const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

            std::vector<MipOffset> mipOffsets;
            uint32_t               levelOffset = 0;
            uint32_t               levelWidth  = ibl.baseWidth;
            uint32_t               levelHeight = ibl.baseHeight;
            for (uint32_t i = 0; i < ibl.numLevels; ++i) {
                MipOffset mipOffset = {};
                mipOffset.Offset    = levelOffset;
                mipOffset.RowStride = rowStride;

                mipOffsets.push_back(mipOffset);

                levelOffset += (rowStride * levelHeight);
                levelWidth >>= 1;
                levelHeight >>= 1;
            }

            MetalTexture texture = {};
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.baseWidth,
                ibl.baseHeight,
                MTL::PixelFormatRGBA32Float,
                mipOffsets,
                ibl.environmentMap.GetSizeInBytes(),
                ibl.environmentMap.GetPixels(),
                &texture));
            outEnvironmentTextures.push_back(texture);
        }

        gIBLNames.push_back(iblFile.filename().replace_extension().string());

        GREX_LOG_INFO("Loaded " << iblFile);
    }
}

void CreateMaterials(
    MetalRenderer*                   pRenderer,
    MaterialTextures&                outDefaultMaterialTextures,
    std::vector<MaterialTextures>&   outMaterialTexturesSets,
    std::vector<MaterialParameters>& outMaterialParametersSets)
{
    // Default material textures
    {
        PixelRGBA8u purplePixel = {0, 0, 0, 255};
        PixelRGBA8u blackPixel  = {0, 0, 0, 255};
        PixelRGBA8u whitePixel  = {255, 255, 255, 255};

        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &purplePixel, &outDefaultMaterialTextures.baseColorTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.normalTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.roughnessTexture));
        CHECK_CALL(CreateTexture(pRenderer, 1, 1, MTL::PixelFormatRGBA8Unorm, sizeof(PixelRGBA8u), &blackPixel, &outDefaultMaterialTextures.metallicTexture));
    }

    // Texture directory
    auto texturesDir = GetAssetPath("textures");

    // Material files - limit to 16 since there's 16 objects draws
    std::vector<std::filesystem::path> materialFiles = {
        texturesDir / "bark_brown_02" / "material.mat",
        texturesDir / "bark_willow" / "material.mat",
        texturesDir / "brick_4" / "material.mat",
        texturesDir / "castle_brick_02_red" / "material.mat",
        texturesDir / "dark_brick_wall" / "material.mat",
        texturesDir / "factory_wall" / "material.mat",
        texturesDir / "green_metal_rust" / "material.mat",
        texturesDir / "hexagonal_concrete_paving" / "material.mat",
        texturesDir / "metal_grate_rusty" / "material.mat",
        texturesDir / "metal_plate" / "material.mat",
        texturesDir / "mud_cracked_dry_riverbed_002" / "material.mat",
        texturesDir / "pavement_02" / "material.mat",
        texturesDir / "rough_plaster_broken" / "material.mat",
        texturesDir / "rusty_metal_02" / "material.mat",
        texturesDir / "weathered_planks" / "material.mat",
        texturesDir / "wood_table_001" / "material.mat",
    };

    size_t maxEntries = NUM_MATERIALS;
    assert(maxEntries <= materialFiles.size());
    for (size_t i = 0; i < maxEntries; ++i) {
        auto materialFile = materialFiles[i];

        std::ifstream is = std::ifstream(materialFile.string().c_str());
        if (!is.is_open()) {
            assert(false && "faild to open material file");
        }

        MaterialTextures   materialTextures = outDefaultMaterialTextures;
        MaterialParameters pMaterialParams  = {};

        while (!is.eof()) {
            MetalTexture*         pTargetTexture = nullptr;
            std::filesystem::path textureFile    = "";

            std::string key;
            is >> key;
            if (key == "basecolor") {
                is >> textureFile;
                pTargetTexture = &materialTextures.baseColorTexture;
            }
            else if (key == "normal") {
                is >> textureFile;
                pTargetTexture = &materialTextures.normalTexture;
            }
            else if (key == "roughness") {
                is >> textureFile;
                pTargetTexture = &materialTextures.roughnessTexture;
            }
            else if (key == "metallic") {
                is >> textureFile;
                pTargetTexture = &materialTextures.metallicTexture;
            }
            else if (key == "specular") {
                is >> pMaterialParams.specular;
            }

            if (textureFile.empty()) {
                continue;
            }

            auto cwd    = materialFile.parent_path().filename();
            textureFile = "textures" / cwd / textureFile;

            auto bitmap = LoadImage8u(textureFile);
            if (!bitmap.Empty()) {
                MipmapRGBA8u mipmap = MipmapRGBA8u(
                    bitmap,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_SAMPLE_MODE_WRAP,
                    BITMAP_FILTER_MODE_NEAREST);

                std::vector<MipOffset> mipOffsets;
                for (auto& srcOffset : mipmap.GetOffsets()) {
                    MipOffset dstOffset = {};
                    dstOffset.Offset    = srcOffset;
                    dstOffset.RowStride = mipmap.GetRowStride();
                    mipOffsets.push_back(dstOffset);
                }

                CHECK_CALL(CreateTexture(
                    pRenderer,
                    mipmap.GetWidth(0),
                    mipmap.GetHeight(0),
                    MTL::PixelFormatRGBA8Unorm,
                    mipOffsets,
                    mipmap.GetSizeInBytes(),
                    mipmap.GetPixels(),
                    &(*pTargetTexture)));

                GREX_LOG_INFO("Created texture from " << textureFile);
            }
            else {
                GREX_LOG_ERROR("Failed to load: " << textureFile);
                assert(false && "Failed to load texture!");
            }
        }

        outMaterialTexturesSets.push_back(materialTextures);
        outMaterialParametersSets.push_back(pMaterialParams);

        // Use directory name for material name
        gMaterialNames.push_back(materialFile.parent_path().filename().string());
    }
}
