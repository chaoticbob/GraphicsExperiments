#include "window.h"

#include "dx_renderer.h"
#include "line_mesh.h"
#include "tri_mesh.h"

#include "dx_draw_context.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;
using float2 = glm::vec2;
using float3 = glm::vec3;

#include "pcg32.h"

#include "sampling.h"

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr)) {                            \
            std::stringstream ss;                    \
            ss << "\n";                              \
            ss << "*** FUNCTION CALL FAILED *** \n"; \
            ss << "FUNCTION: " << #FN << "\n";       \
            ss << "\n";                              \
            GREX_LOG_ERROR(ss.str().c_str());        \
            assert(false);                           \
        }                                            \
    }

// =============================================================================
// Shader code
// =============================================================================
const char* gDrawSamplesShaders = R"(

struct CameraProperties {
	float4x4 MVP;
};

ConstantBuffer<CameraProperties> Cam : register(b0); // Constant buffer

struct VSOutput {
    float4 PositionCS : SV_POSITION;
    float3 Color      : COLOR;
    float2 TexCoord   : TEXCOORD;
};

VSOutput vsmain(float3 PositionOS : POSITION, float3 Color : COLOR0, float2 TexCoord : TEXCOORD)
{
    VSOutput output = (VSOutput)0;
    output.PositionCS = mul(Cam.MVP, float4(PositionOS, 1));
    output.Color = Color;
    output.TexCoord = TexCoord;
    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float d = sqrt(distance(uv, float2(0.5, 0.5)));
    float a = d < 0.5 ? 1 : 0;
    return float4(input.Color, 0.5 * a);   
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static uint32_t gNumSamples    = 1024;
static uint32_t gGenNumSamples = UINT32_MAX;

static uint32_t gSequenceIndex    = 0;
static uint32_t gGenSequenceIndex = UINT32_MAX;

static uint32_t gHemisphereIndex    = 0;
static uint32_t gGenHemisphereIndex = UINT32_MAX;

static GenerateSamples2DFn gGenSamples2DFn = std::bind(GenerateSamples2DUniform, std::placeholders::_1, std::placeholders::_2);

static float gGGXRoughness    = 0.5f;
static float gGenGGXRoughness = 0.0f;

static float gSampleDrawScale    = 0.03f;
static float gGenSampleDrawScale = 0.0f;

enum SequenceName
{
    SEQUENCE_NAME_UNIFORM    = 0,
    SEQUENCE_NAME_HAMMERSLEY = 1,
    SEQUENCE_NAME_CMJ        = 2,
};

static std::vector<std::string> gSequenceNames = {
    "Uniform",
    "Hammersley",
    "CMJ",
};

enum HemisphereName
{
    HEMISPHERE_NAME_UNIFORM        = 0,
    HEMISPHERE_NAME_COS_WEIGHTED   = 1,
    HEMISPHERE_NAME_IMPORTANCE_GGX = 2,
};

static std::vector<std::string> gHemisphereNames = {
    "Uniform",
    "Cosine Weighted",
    "ImportanceGGX",
};

struct LineGeometry
{
    uint32_t               numIndices = 0;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> vertexBuffer;
};

struct TriGeometry
{
    uint32_t               numIndices = 0;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> vertexColorBuffer;
    ComPtr<ID3D12Resource> texCoordBuffer;
};

void DrawSamples(DxDrawContext* pCtx, uint32_t numSamples, float drawScale);

// =============================================================================
// Event functions
// =============================================================================
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
    // *************************************************************************
    // Renderer
    // *************************************************************************
    std::unique_ptr<DxRenderer> renderer = std::make_unique<DxRenderer>();

    if (!InitDx(renderer.get(), gEnableDebug)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "sampling_vis");
    if (!window) {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight())) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain RTV Heap
    // *************************************************************************
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
    {
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForD3D12(renderer.get())) {
        assert(false && "GrexWindow::InitImGuiForD3D12 failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command allocator
    // *************************************************************************
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    {
        CHECK_CALL(renderer->Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,    // type
            IID_PPV_ARGS(&commandAllocator))); // ppCommandList
    }

    // *************************************************************************
    // Command list
    // *************************************************************************
    ComPtr<ID3D12GraphicsCommandList5> commandList;
    {
        CHECK_CALL(renderer->Device->CreateCommandList1(
            0,                              // nodeMask
            D3D12_COMMAND_LIST_TYPE_DIRECT, // type
            D3D12_COMMAND_LIST_FLAG_NONE,   // flags
            IID_PPV_ARGS(&commandList)));   // ppCommandList
    }

    // *************************************************************************
    // Draw contexts
    // *************************************************************************
    DxDrawContext drawContext(renderer.get(), GREX_DEFAULT_RTV_FORMAT, GREX_DEFAULT_DSV_FORMAT);

    int32_t drawSamplesProgram = drawContext.CreateProgram(gDrawSamplesShaders, "vsmain", "psmain");
    assert((drawSamplesProgram >= 0) && "create program failed: draw samples");

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        window->ImGuiNewFrameD3D12();

        std::string exePath = GetExecutablePath().filename().string();

        if (ImGui::Begin("Params")) {
            ImGui::DragInt("Num Samples", reinterpret_cast<int*>(&gNumSamples), 1, 1, 8192);

            ImGui::Separator();

            static const char* currentSequenceName   = gSequenceNames[gSequenceIndex].c_str();
            static const char* currentHemisphereName = gHemisphereNames[gHemisphereIndex].c_str();

            if (ImGui::BeginCombo("Sequence Fn", currentSequenceName)) {
                for (size_t i = 0; i < gSequenceNames.size(); ++i) {
                    bool isSelected = (currentSequenceName == gSequenceNames[i]);
                    if (ImGui::Selectable(gSequenceNames[i].c_str(), isSelected)) {
                        currentSequenceName = gSequenceNames[i].c_str();
                        gSequenceIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::BeginCombo("Hemisphere Fn", currentHemisphereName)) {
                for (size_t i = 0; i < gHemisphereNames.size(); ++i) {
                    bool isSelected = (currentHemisphereName == gHemisphereNames[i]);
                    if (ImGui::Selectable(gHemisphereNames[i].c_str(), isSelected)) {
                        currentHemisphereName = gHemisphereNames[i].c_str();
                        gHemisphereIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            ImGui::SliderFloat("GGX Roughness", &gGGXRoughness, 0.0f, 1.0f);

            ImGui::Separator();

            ImGui::SliderFloat("Draw Scale", &gSampleDrawScale, 0.01f, 0.5f);
        }
        ImGui::End();

        // ---------------------------------------------------------------------
        bool generateHemisphere = false;

        if (gNumSamples != gGenNumSamples) {
            gGenNumSamples     = gNumSamples;
            generateHemisphere = true;
        }

        if (gSequenceIndex != gGenSequenceIndex) {
            gGenSequenceIndex = gSequenceIndex;

            switch (gSequenceIndex) {
                default: break;

                case SEQUENCE_NAME_UNIFORM: {
                    gGenSamples2DFn    = std::bind(GenerateSamples2DUniform, std::placeholders::_1, std::placeholders::_2);
                    generateHemisphere = true;
                } break;
                case SEQUENCE_NAME_HAMMERSLEY: {
                    gGenSamples2DFn    = std::bind(GenerateSamples2DHammersley, std::placeholders::_1, std::placeholders::_2);
                    generateHemisphere = true;
                } break;
                case SEQUENCE_NAME_CMJ: {
                    gGenSamples2DFn    = std::bind(GenerateSamples2DCMJ, std::placeholders::_1, std::placeholders::_2);
                    generateHemisphere = true;
                } break;
            }
        }

        if (gHemisphereIndex != gGenHemisphereIndex) {
            gGenHemisphereIndex = gHemisphereIndex;
            generateHemisphere  = true;
        }

        if ((gHemisphereIndex == HEMISPHERE_NAME_IMPORTANCE_GGX) && (fabs(gGGXRoughness - gGenGGXRoughness) > 0.00001f)) {
            gGenGGXRoughness   = gGGXRoughness;
            generateHemisphere = true;
        }

        if (fabs(gSampleDrawScale - gGenSampleDrawScale) > 0.00001f) {
            gGenSampleDrawScale = gSampleDrawScale;
            generateHemisphere  = true;
        }

        //if (generateHemisphere) {
        //    CreateHemisphereGeo(renderer.get(), gGenNumSamples, gGenSampleDrawScale, hemisphereGeo);
        //}

        // ---------------------------------------------------------------------

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.1f;

        // Camera matrices
        vec3 eyePosition = vec3(0, 2, 1.5f);
        mat4 viewMat     = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat     = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);
        mat4 rotMat      = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));
        mat4 mvpMat      = projMat * viewMat * rotMat;

        // ---------------------------------------------------------------------

        // Draw to swapchain
        {
            UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

            ComPtr<ID3D12Resource> swapchainBuffer;
            CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

            CHECK_CALL(commandAllocator->Reset());
            CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

            D3D12_RESOURCE_BARRIER preRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandList->ResourceBarrier(1, &preRenderBarrier);
            {
                // Set RTV and DSV
                commandList->OMSetRenderTargets(
                    1,
                    &renderer->SwapchainRTVDescriptorHandles[bufferIndex],
                    false,
                    &renderer->SwapchainDSVDescriptorHandles[bufferIndex]);

                // Clear RTV and DSV
                float clearColor[4] = {0.23f, 0.23f, 0.26f, 0};
                commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 0, nullptr);
                commandList->ClearDepthStencilView(renderer->SwapchainDSVDescriptorHandles[bufferIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 0, nullptr);

                // View port and scissor
                D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
                commandList->RSSetViewports(1, &viewport);

                D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
                commandList->RSSetScissorRects(1, &scissor);

                drawContext.Reset();

                // Draw grid
                drawContext.SetProgram(DxDrawContext::GetStockProgramDrawVertexColor());
                drawContext.SetDepthRead(true);
                drawContext.SetDepthWrite(true);
                drawContext.SetBlendNone();
                drawContext.SetMatrix(mvpMat);
                drawContext.DrawGridXZ(float2(2), 12, 12);

                // Draw samples
                drawContext.SetProgram(drawSamplesProgram);
                drawContext.SetDepthRead(false);
                drawContext.SetDepthWrite(false);
                drawContext.SetBlendAdditive();
                DrawSamples(&drawContext, gGenNumSamples, gGenSampleDrawScale);

                drawContext.FlushToCommandList(commandList.Get());

                // ImGui
                window->ImGuiRenderDrawData(renderer.get(), commandList.Get());
            }
            D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            commandList->ResourceBarrier(1, &postRenderBarrier);

            commandList->Close();

            ID3D12CommandList* pList = commandList.Get();
            renderer->Queue->ExecuteCommandLists(1, &pList);

            if (!WaitForGpu(renderer.get())) {
                assert(false && "WaitForGpu failed");
                break;
            }
        }

        if (!SwapchainPresent(renderer.get())) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void DrawSamples(DxDrawContext* pCtx, uint32_t numSamples, float drawScale)
{
    struct Vertex
    {
        float3 pos;
        float3 color;
        float2 uv;
    };

    // clang-format off
    std::vector<Vertex> squareVertices = {
        // Triangle 1
        {float3(-0.5f, 0.5f,  0.0f), float3(1.0f), float2(0.0f, 0.0f), },
        {float3(-0.5f, -0.5f, 0.0f), float3(1.0f), float2(0.0f, 1.0f), },
        {float3(0.5f,  -0.5f, 0.0f), float3(1.0f), float2(1.0f, 1.0f), },
        // Triangle 2
        {float3(-0.5f, 0.5f,  0.0f), float3(1.0f), float2(0.0f, 0.0f), },
        {float3(0.5f,  -0.5f, 0.0f), float3(1.0f), float2(1.0f, 1.0f), },
        {float3(0.5f,  0.5f,  0.0f), float3(1.0f), float2(1.0f, 0.0f), },
    };
    // clang-format on

    vec3 N = vec3(0, 1, 0);

    std::vector<float3> samples;
    switch (gGenHemisphereIndex) {
        default: break;

        case HEMISPHERE_NAME_UNIFORM: {
            samples = GenerateSamplesHemisphereUniform(N, numSamples, gGenSamples2DFn);
        } break;

        case HEMISPHERE_NAME_COS_WEIGHTED: {
            samples = GenerateSamplesHemisphereCosineWeighted(N, numSamples, gGenSamples2DFn);
        } break;

        case HEMISPHERE_NAME_IMPORTANCE_GGX: {
            samples = GenerateSamplesHemisphereImportanceGGX(N, gGGXRoughness, numSamples, gGenSamples2DFn);
        } break;
    }

    pCtx->BeginTriangles();
    for (auto& center : samples) {
        float3 W  = glm::normalize(center);
        float3 up = (abs(W.y) < 0.9999f) ? float3(0, 1, 0) : float3(0, 0, -1);
        float3 U  = (abs(W.y) < 0.9999f) ? glm::normalize(glm::cross(up, float3(0, 0, 1))) : float3(1, 0, 0);
        float3 V  = glm::normalize(glm::cross(W, U));
        U         = glm::normalize(glm::cross(V, W));

        // clang-format off
        mat3 M = mat3(
            U.x, U.y, U.z, 
            V.x, V.y, V.z, 
            W.x, W.y, W.z);
        // clang-format on

        for (auto vtx : squareVertices) {
            float3 P = (drawScale * vtx.pos);
            P        = M * P;
            P += center;

            pCtx->Color(vtx.color);
            pCtx->TexCoord(vtx.uv);
            pCtx->Vertex(P);
        }
    }
    pCtx->EndTriangles();
}