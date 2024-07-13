#include "window.h"

#include "mtl_renderer.h"
#include "bitmap.h"
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

#define DISTRIBUTION_TROWBRIDGE_REITZ 0
#define DISTRIBUTION_BECKMANN         1
#define DISTRIBUTION_BLINN_PHONG      2

#define FRESNEL_SCHLICK_ROUGHNESS 0
#define FRESNEL_SCHLICK           1
#define FRESNEL_COOK_TORRANCE     2
#define FRESNEL_NONE              3

#define GEOMETRY_SMITH                 0
#define GEOMETRY_IMPLICIT              1
#define GEOMETRY_NEUMANN               2
#define GEOMETRY_COOK_TORRANCE         3
#define GEOMETRY_KELEMEN               4
#define GEOMETRY_BECKMANN              5
#define GEOMETRY_GGX1                  6
#define GEOMETRY_GGX2                  7
#define GEOMETRY_SCHLICK_GGX           8
#define GEOMETRY_SMITH_CORRELATED      9
#define GEOMETRY_SMITH_CORRELATED_FAST 10

// This will be passed in via constant buffer
struct DrawParameters
{
    mat4     ModelMatrix;
    uint32_t MaterialIndex;
    uint32_t __pad0[3];
};

struct Light
{
    vec3     position;
    uint32_t __pad0;
    vec3     color;
    uint32_t __pad1;
    float    intensity;
    uint32_t __pad2[3];
};

struct SceneParameters
{
    mat4     viewProjectionMatrix;
    vec3     eyePosition;
    uint32_t __pad0;
    uint32_t numLights;
    uint32_t __pad1[3];
    Light    lights[8];
    uint32_t iblEnvironmentNumLevels;
    uint32_t iblIndex;
    float    iblDiffuseStrength;
    float    iblSpecularStrength;
};

struct MaterialParameters
{
    vec3     baseColor;
    uint32_t __pad0;
    float    roughness;
    float    metallic;
    float    specular;
    uint32_t directComponentMode;
    uint32_t D_Func;
    uint32_t F_Func;
    uint32_t G_Func;
    uint32_t indirectComponentMode;
    uint32_t indirectSpecularMode;
    uint32_t drawMode;
    uint32_t __pad1[2];
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

const std::vector<std::string> gDistributionNames = {
    "GGX (Trowbridge-Reitz)",
    "Beckmann",
    "Blinn-Phong",
};

const std::vector<std::string> gFresnelNames = {
    "Schlick with Roughness",
    "Schlick",
    "CookTorrance",
    "None",
};

const std::vector<std::string> gGeometryNames = {
    "Smith",
    "Implicit",
    "Neumann",
    "Cook-Torrance",
    "Kelemen",
    "Beckmann",
    "GGX1",
    "GGX2",
    "SchlickGGX",
    "Smith Correlated",
    "Smith Correlated Fast",
};

const std::vector<std::string> gDirectComponentModeNames = {
    "All",
    "Distribution",
    "Fresnel",
    "Geometry",
    "Diffuse",
    "Radiance",
    "kD",
    "Specular",
    "BRDF",
};

const std::vector<std::string> gIndirectComponentModeNames = {
    "All",
    "Diffuse",
    "Specular"};

const std::vector<std::string> gIndirectSpecularModeNames = {
    "LUT",
    "Approx Lazarov",
    "Approx Polynomial",
    "Approx Karis",
};

const std::vector<std::string> gDrawModeNames = {
    "Full Lighting",
    "Direct",
    "Indirect",
};

const std::vector<std::string> gModelNames = {
    "Sphere",
    "Knob",
    "Monkey",
    "Teapot",
};

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

// clang-format off
static std::vector<MaterialParameters> gMaterialParams = {
    {F0_MetalCopper,         0, 0.25f, 1.00f, 0.5f, 0, 0, 0, 0},
    {F0_MetalGold,           0, 0.05f, 1.00f, 0.5f, 0, 0, 0, 0},
    {F0_MetalSilver,         0, 0.18f, 1.00f, 0.5f, 0, 0, 0, 0},
    {F0_MetalZinc,           0, 0.65f, 1.00f, 0.5f, 0, 0, 0, 0},
    {F0_MetalTitanium,       0, 0.11f, 1.00f, 0.5f, 0, 0, 0, 0},
    {vec3(0.6f, 0.0f, 0.0f), 0, 0.00f, 0.00f, 0.5f, 0, 0, 0, 0},
    {vec3(0.0f, 0.6f, 0.0f), 0, 0.25f, 0.00f, 0.5f, 0, 0, 0, 0},
    {vec3(0.0f, 0.0f, 0.6f), 0, 0.50f, 0.00f, 0.5f, 0, 0, 0, 0},
    {vec3(0.7f, 0.7f, 0.2f), 0, 0.92f, 0.15f, 0.5f, 0, 0, 0, 0},
};
// clang-format on

static std::vector<std::string> gMaterialNames = {
    "Copper",
    "Gold",
    "Silver",
    "Zink",
    "Titanium",
    "Shiny Plastic",
    "Rough Plastic",
    "Rougher Plastic",
    "Roughest Plastic",
};

static uint32_t                 gNumLights           = 0;
static const uint32_t           gMaxIBLs             = 32;
static uint32_t                 gIBLIndex            = 0;
static std::vector<std::string> gIBLNames            = {};
static float                    gIBLDiffuseStrength  = 1.0f;
static float                    gIBLSpecularStrength = 1.0f;
static uint32_t                 gModelIndex          = 0;

void CreateEnvironmentVertexBuffers(
    MetalRenderer*   pRenderer,
    GeometryBuffers& outGeomtryBuffers);
void CreateMaterialModels(
    MetalRenderer*                pRenderer,
    std::vector<GeometryBuffers>& outGeomtryBuffers);
void CreateIBLTextures(
    MetalRenderer*             pRenderer,
    MetalTexture*              pBRDFLUT,
    std::vector<MetalTexture>& outIrradianceTextures,
    std::vector<MetalTexture>& outEnvironmentTextures,
    std::vector<uint32_t>&     outEnvNumLevels);

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
        std::string shaderSource = LoadString("projects/251_pbr_explorer/shaders.metal");
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
        std::string shaderSource = LoadString("projects/251_pbr_explorer/drawtexture.metal");
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
    CHECK_CALL(CreateDrawNormalPipeline(
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
    std::vector<MetalTexture> irrTextures;
    std::vector<MetalTexture> envTextures;
    std::vector<uint32_t>     envNumLevels;
    CreateIBLTextures(renderer.get(), &brdfLUT, irrTextures, envTextures, envNumLevels);

    // *************************************************************************
    // Texture Arrays
    // *************************************************************************

    // Irradiance
    std::vector<MTL::Texture*> irrMetalTextures;
    for (size_t i = 0; i < irrTextures.size(); ++i) {
        irrMetalTextures.push_back(irrTextures[i].Texture.get());
    }

    // Environment
    std::vector<MTL::Texture*> envMetalTextures;
    for (size_t i = 0; i < envTextures.size(); ++i) {
        envMetalTextures.push_back(envTextures[i].Texture.get());
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
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT)) {
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

    while (window->PollEvents()) {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

        if (ImGui::Begin("Scene")) {
            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName)) {
                for (size_t i = 0; i < gIBLNames.size(); ++i) {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected)) {
                        currentIBLName = gIBLNames[i].c_str();
                        gIBLIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SliderFloat("IBL Diffuse Strength", &gIBLDiffuseStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("IBL Specular Strength", &gIBLSpecularStrength, 0.0f, 2.0f);
            ImGui::SliderInt("Number of Lights", reinterpret_cast<int*>(&gNumLights), 0, 4);

            ImGui::Separator();

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
        }
        ImGui::End();

        if (ImGui::Begin("Material Parameters")) {
            static std::vector<const char*> currentDrawModeNames(gMaterialParams.size(), gDrawModeNames[0].c_str());
            static std::vector<const char*> currentDirectComponentModeNames(gMaterialParams.size(), gDirectComponentModeNames[0].c_str());
            static std::vector<const char*> currentDistributionNames(gMaterialParams.size(), gDistributionNames[0].c_str());
            static std::vector<const char*> currentFresnelNames(gMaterialParams.size(), gFresnelNames[0].c_str());
            static std::vector<const char*> currentGeometryNames(gMaterialParams.size(), gGeometryNames[0].c_str());
            static std::vector<const char*> currentIndirectComponentModeNames(gMaterialParams.size(), gIndirectComponentModeNames[0].c_str());
            static std::vector<const char*> currentIndirectSpecularModeNames(gMaterialParams.size(), gIndirectSpecularModeNames[0].c_str());

            for (uint32_t matIdx = 0; matIdx < gMaterialNames.size(); ++matIdx) {
                if (ImGui::TreeNodeEx(gMaterialNames[matIdx].c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    // DrawMode
                    if (ImGui::BeginCombo("DrawMode", currentDrawModeNames[matIdx])) {
                        for (size_t i = 0; i < gDrawModeNames.size(); ++i) {
                            bool isSelected = (currentDrawModeNames[matIdx] == gDrawModeNames[i]);
                            if (ImGui::Selectable(gDrawModeNames[i].c_str(), isSelected)) {
                                currentDrawModeNames[matIdx]     = gDrawModeNames[i].c_str();
                                gMaterialParams[matIdx].drawMode = static_cast<uint32_t>(i);
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    // Direct Light Params
                    if (ImGui::TreeNodeEx("Direct Light Parames", ImGuiTreeNodeFlags_DefaultOpen)) {
                        // DirectComponentMode
                        if (ImGui::BeginCombo("Direct Component Mode", currentDirectComponentModeNames[matIdx])) {
                            for (size_t i = 0; i < gDirectComponentModeNames.size(); ++i) {
                                bool isSelected = (currentDirectComponentModeNames[matIdx] == gDirectComponentModeNames[i]);
                                if (ImGui::Selectable(gDirectComponentModeNames[i].c_str(), isSelected)) {
                                    currentDirectComponentModeNames[matIdx]     = gDirectComponentModeNames[i].c_str();
                                    gMaterialParams[matIdx].directComponentMode = static_cast<uint32_t>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        // Distribution
                        if (ImGui::BeginCombo("Distribution", currentDistributionNames[matIdx])) {
                            for (size_t i = 0; i < gDistributionNames.size(); ++i) {
                                bool isSelected = (currentDistributionNames[matIdx] == gDistributionNames[i]);
                                if (ImGui::Selectable(gDistributionNames[i].c_str(), isSelected)) {
                                    currentDistributionNames[matIdx] = gDistributionNames[i].c_str();
                                    gMaterialParams[matIdx].D_Func   = static_cast<uint32_t>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        // Fresnel
                        if (ImGui::BeginCombo("Fresnel", currentFresnelNames[matIdx])) {
                            for (size_t i = 0; i < gFresnelNames.size(); ++i) {
                                bool isSelected = (currentFresnelNames[matIdx] == gFresnelNames[i]);
                                if (ImGui::Selectable(gFresnelNames[i].c_str(), isSelected)) {
                                    currentFresnelNames[matIdx]    = gFresnelNames[i].c_str();
                                    gMaterialParams[matIdx].F_Func = static_cast<uint32_t>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        // Geometry
                        if (ImGui::BeginCombo("Geometry", currentGeometryNames[matIdx])) {
                            for (size_t i = 0; i < gGeometryNames.size(); ++i) {
                                bool isSelected = (currentGeometryNames[matIdx] == gGeometryNames[i]);
                                if (ImGui::Selectable(gGeometryNames[i].c_str(), isSelected)) {
                                    currentGeometryNames[matIdx]   = gGeometryNames[i].c_str();
                                    gMaterialParams[matIdx].G_Func = static_cast<uint32_t>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::TreePop();
                    }
                    // Indirect Light Params
                    if (ImGui::TreeNodeEx("Indirect Light Parames", ImGuiTreeNodeFlags_DefaultOpen)) {
                        // IndirectComponentMode
                        if (ImGui::BeginCombo("Indirect Component Mode", currentIndirectComponentModeNames[matIdx])) {
                            for (size_t i = 0; i < gIndirectComponentModeNames.size(); ++i) {
                                bool isSelected = (currentIndirectComponentModeNames[matIdx] == gIndirectComponentModeNames[i]);
                                if (ImGui::Selectable(gIndirectComponentModeNames[i].c_str(), isSelected)) {
                                    currentIndirectComponentModeNames[matIdx]     = gIndirectComponentModeNames[i].c_str();
                                    gMaterialParams[matIdx].indirectComponentMode = static_cast<uint32_t>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        // Specular Mode
                        if (ImGui::BeginCombo("Specular Mode", currentIndirectSpecularModeNames[matIdx])) {
                            for (size_t i = 0; i < gIndirectSpecularModeNames.size(); ++i) {
                                bool isSelected = (currentIndirectSpecularModeNames[matIdx] == gIndirectSpecularModeNames[i]);
                                if (ImGui::Selectable(gIndirectSpecularModeNames[i].c_str(), isSelected)) {
                                    currentIndirectSpecularModeNames[matIdx]     = gIndirectSpecularModeNames[i].c_str();
                                    gMaterialParams[matIdx].indirectSpecularMode = static_cast<uint32_t>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::TreePop();
                    }

                    ImGui::SliderFloat("Roughness", &(gMaterialParams[matIdx].roughness), 0.0f, 1.0f);
                    ImGui::SliderFloat("Metallic", &(gMaterialParams[matIdx].metallic), 0.0f, 1.0f);
                    ImGui::SliderFloat("Specular", &(gMaterialParams[matIdx].specular), 0.0f, 1.0f);
                    ImGui::ColorPicker3("Albedo", reinterpret_cast<float*>(&(gMaterialParams[matIdx].baseColor)), ImGuiColorEditFlags_NoInputs);

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
        vec3 startingEyePosition = vec3(0, 3, 8);
        vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
        mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set constant buffer values
        SceneParameters sceneParams         = {};
        sceneParams.viewProjectionMatrix    = projMat * viewMat;
        sceneParams.eyePosition             = eyePosition;
        sceneParams.numLights               = gNumLights;
        sceneParams.lights[0].position      = vec3(3, 10, 0);
        sceneParams.lights[0].color         = vec3(1, 1, 1);
        sceneParams.lights[0].intensity     = 1.5f;
        sceneParams.lights[1].position      = vec3(-8, 1, 4);
        sceneParams.lights[1].color         = vec3(0.85f, 0.95f, 0.81f);
        sceneParams.lights[1].intensity     = 0.4f;
        sceneParams.lights[2].position      = vec3(0, 8, -8);
        sceneParams.lights[2].color         = vec3(0.89f, 0.89f, 0.97f);
        sceneParams.lights[2].intensity     = 0.95f;
        sceneParams.lights[3].position      = vec3(15, 0, 0);
        sceneParams.lights[3].color         = vec3(0.92f, 0.5f, 0.7f);
        sceneParams.lights[3].intensity     = 0.5f;
        sceneParams.iblEnvironmentNumLevels = envNumLevels[gIBLIndex];
        sceneParams.iblIndex                = gIBLIndex;
        sceneParams.iblDiffuseStrength      = gIBLDiffuseStrength;
        sceneParams.iblSpecularStrength     = gIBLSpecularStrength;

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
            } sceneParams;

            sceneParams.MVP      = projMat * viewMat * moveUp;
            sceneParams.iblIndex = gIBLIndex;

            pRenderEncoder->setVertexBytes(&sceneParams, sizeof(sceneParams), 2);
            pRenderEncoder->setFragmentBytes(&sceneParams, sizeof(sceneParams), 2);

            // Textures
            pRenderEncoder->setFragmentTextures(DataPtr(envMetalTextures), NS::Range(0, 32));

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
            // SceneParams [[buffer(3)]]
            pRenderEncoder->setVertexBytes(&sceneParams, sizeof(SceneParameters), 3);
            pRenderEncoder->setFragmentBytes(&sceneParams, sizeof(SceneParameters), 3);
            // MaterialParameters [[buffer(4)]]
            pRenderEncoder->setFragmentBytes(&gMaterialParams, sizeof(MaterialParameters), 4);
            // IBL textures [[texture(0,1,2)]]
            pRenderEncoder->setFragmentTexture(brdfLUT.Texture.get(), 0);
            pRenderEncoder->setFragmentTextures(DataPtr(irrMetalTextures), NS::Range(16, 32));
            pRenderEncoder->setFragmentTextures(DataPtr(envMetalTextures), NS::Range(48, 32));

            // Select which model to draw
            const GeometryBuffers& geoBuffers = matGeoBuffers[gModelIndex];

            // Vertex buffers
            MTL::Buffer* vbvs[2]    = {};
            NS::UInteger offsets[2] = {};
            NS::Range    vbRange(0, 2);
            // Position
            vbvs[0]    = geoBuffers.positionBuffer.Buffer.get();
            offsets[0] = 0;
            // Tex coord
            vbvs[1]    = geoBuffers.normalBuffer.Buffer.get();
            offsets[1] = 0;
            pRenderEncoder->setVertexBuffers(vbvs, offsets, vbRange);

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeFront);

            // Pipeline state
            pRenderEncoder->setRenderPipelineState(pbrPipelineState.State.get());
            pRenderEncoder->setDepthStencilState(pbrDepthStencilState.State.get());

            pRenderEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
            pRenderEncoder->setCullMode(MTL::CullModeBack);

            const float yPos = 0.0f;

            // Copper
            {
                glm::mat4 modelMat      = glm::translate(vec3(-3, yPos, 3));
                uint32_t  materialIndex = 0;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Gold
            {
                glm::mat4 modelMat      = glm::translate(vec3(0, yPos, 3));
                uint32_t  materialIndex = 1;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Silver
            {
                glm::mat4 modelMat      = glm::translate(vec3(3, yPos, 3));
                uint32_t  materialIndex = 2;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Zink
            {
                glm::mat4 modelMat      = glm::translate(vec3(-3, yPos, 0));
                uint32_t  materialIndex = 3;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Titanium
            {
                glm::mat4 modelMat      = glm::translate(vec3(0, yPos, 0));
                uint32_t  materialIndex = 4;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Shiny Plastic
            {
                glm::mat4 modelMat      = glm::translate(vec3(3, yPos, 0));
                uint32_t  materialIndex = 5;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Rough Plastic
            {
                glm::mat4 modelMat      = glm::translate(vec3(-3, yPos, -3));
                uint32_t  materialIndex = 6;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Rougher Plastic
            {
                glm::mat4 modelMat      = glm::translate(vec3(0, yPos, -3));
                uint32_t  materialIndex = 7;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
            }

            // Roughest Plastic
            {
                glm::mat4 modelMat      = glm::translate(vec3(3, yPos, -3));
                uint32_t  materialIndex = 8;

                DrawParameters drawParams = {};
                drawParams.ModelMatrix    = modelMat;
                drawParams.MaterialIndex  = materialIndex;

                // DrawParams [[buffers(2)]]
                pRenderEncoder->setVertexBytes(&drawParams, sizeof(DrawParameters), 2);
                pRenderEncoder->setFragmentBytes(&drawParams, sizeof(DrawParameters), 2);
                // MaterialParams [[buffer(4)]]
                pRenderEncoder->setFragmentBytes(DataPtr(gMaterialParams), CountU32(gMaterialParams) * sizeof(MaterialParameters), 4);

                pRenderEncoder->drawIndexedPrimitives(
                    MTL::PrimitiveType::PrimitiveTypeTriangle,
                    geoBuffers.numIndices,
                    MTL::IndexTypeUInt32,
                    geoBuffers.indexBuffer.Buffer.get(),
                    0);
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
        options.enableNormals    = true;

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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Knob
    {
        TriMesh::Options options = {};
        options.enableNormals    = true;
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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Monkey
    {
        TriMesh::Options options = {};
        options.enableNormals    = true;
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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }

    // Teapot
    {
        TriMesh::Options options = {};
        options.enableNormals    = true;
        options.applyTransform   = true;
        options.transformRotate  = glm::vec3(0, glm::radians(135.0f), 0);

        TriMesh mesh;
        if (!TriMesh::LoadOBJ(GetAssetPath("models/teapot.obj").string(), "", options, &mesh)) {
            return;
        }
        mesh.ScaleToFit(2.0f);

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
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &buffers.normalBuffer));

        outGeomtryBuffers.push_back(buffers);
    }
}

void CreateIBLTextures(
    MetalRenderer*             pRenderer,
    MetalTexture*              pBRDFLUT,
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
        auto& iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl)) {
            GREX_LOG_ERROR("failed to load: " << iblFile);
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
