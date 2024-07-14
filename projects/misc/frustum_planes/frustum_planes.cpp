
#define GLM_FORCE_SWIZZLE

#include "window.h"

#include "dx_renderer.h"
#include "line_mesh.h"
#include "tri_mesh.h"
#include "camera.h"

#include "dx_draw_context.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;
using float2 = glm::vec2;
using float3 = glm::vec3;

#include "imGuIZMOquat.h"

#include "pcg32.h"

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr))                              \
        {                                            \
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

static quat   gCamRotation      = quat(1.f, 0.f, 0.f, 0.f);
static float3 gCamEyePosition   = float3(0, 0, 1);
static float3 gCamTarget        = float3(0, 0, 0);
static float  gCamFov           = 45.0f;
static float  gCamAspect        = 1.77f;
static float  gCamNear          = 0.1f;
static float  gCamFar           = 1.5f;
static bool   gFitConeToFarClip = false;

static uint32_t gNumSpheres    = 128;
static uint32_t gGenNumSpheres = 0;

enum VisibilityFunc
{
    VISIBILITY_FUNC_PLANES              = 0,
    VISIBILITY_FUNC_SPHERE              = 1,
    VISIBILITY_FUNC_CONE                = 2,
    VISIBILITY_FUNC_CONE_AND_NEAR_PLANE = 3,
};

static std::vector<std::string> gVisibilityFuncNames = {
    "Frustum Planes",
    "Frustum Sphere",
    "Frustum Cone",
    "Frustum Cone and Near Plane",
};

static int gVisibilityFunc = VISIBILITY_FUNC_PLANES;

// =============================================================================
// Event functions
// =============================================================================
void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT)
    {
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

    if (!InitDx(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "frustum_planes");
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight()))
    {
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
    if (!window->InitImGuiForD3D12(renderer.get()))
    {
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
    DxDrawContext ctx(renderer.get(), GREX_DEFAULT_RTV_FORMAT, GREX_DEFAULT_DSV_FORMAT);

    int32_t drawSamplesProgram = ctx.CreateProgram(gDrawSamplesShaders, "vsmain", "psmain");
    assert((drawSamplesProgram >= 0) && "create program failed: draw samples");

    // *************************************************************************
    // Draw contexts
    // *************************************************************************
    // xyz = pos, w = radius
    std::vector<float4> spheres;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents())
    {
        window->ImGuiNewFrameD3D12();

        std::string exePath = GetExecutablePath().filename().string();

        if (ImGui::Begin("Params"))
        {
            ImGui::DragFloat3("Eye Position", (float*)&gCamEyePosition, 0.01f);
            if (ImGui::Button("Reset Eye Position"))
            {
                gCamEyePosition = vec3(0, 0, 1);
            }

            ImGui::Text("Rotation");
            imguiGizmo::setGizmoFeelingRot(0.8f);
            ImGui::gizmo3D("##gizmo1", gCamRotation, 128.0f);
            if (ImGui::Button("Reset Rotation"))
            {
                gCamRotation = quat(1, 0, 0, 0);
            }

            ImGui::Separator();

            ImGui::DragFloat("FOV", &gCamFov, 0.5f, 1.0f, 180.0f);
            ImGui::DragFloat("Aspect", &gCamAspect, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Near Clip", &gCamNear, 0.01f, 0.01f, 1.0f);
            ImGui::DragFloat("Far Clip", &gCamFar, 0.01f, 1.01f, 4.0f);

            ImGui::Separator();

            ImGui::DragInt("Num Spheres", reinterpret_cast<int*>(&gNumSpheres), 1, 0, 1024);

            ImGui::Separator();

            // Visibility Func
            static const char* currentVisibilityFuncName = gVisibilityFuncNames[gVisibilityFunc].c_str();
            if (ImGui::BeginCombo("Visibility Func", currentVisibilityFuncName))
            {
                for (size_t i = 0; i < gVisibilityFuncNames.size(); ++i)
                {
                    bool isSelected = (currentVisibilityFuncName == gVisibilityFuncNames[i]);
                    if (ImGui::Selectable(gVisibilityFuncNames[i].c_str(), isSelected))
                    {
                        currentVisibilityFuncName = gVisibilityFuncNames[i].c_str();
                        gVisibilityFunc           = static_cast<uint32_t>(i);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            ImGui::Checkbox("Fit Cone to Far Clip", &gFitConeToFarClip);
        }
        ImGui::End();

        bool genSpheres = false;
        if (gGenNumSpheres != gNumSpheres)
        {
            gGenNumSpheres = gNumSpheres;
            genSpheres     = true;
        }

        if (genSpheres)
        {
            pcg32 rng = pcg32(0x7C0FFE35);

            spheres.clear();
            for (uint32_t i = 0; i < gGenNumSpheres; ++i)
            {
                float x = rng.nextFloat();
                float y = rng.nextFloat();
                float z = rng.nextFloat();
                float r = rng.nextFloat();

                x = glm::mix(-1.0f, 1.0f, x) * 0.70f;
                y = glm::mix(-1.0f, 1.0f, y) * 0.70f;
                z = glm::mix(-1.0f, 1.0f, z) * 0.70f;
                r = glm::mix(0.07f, 0.15f, z);

                spheres.push_back(float4(x, y, z, r));
            }
        }

        // ---------------------------------------------------------------------

        // View camera
        const vec3  eyePosition = vec3(1, 1.5f, 1.5f);
        const float fov         = 90;
        const float aspect      = gWindowWidth / static_cast<float>(gWindowHeight);
        PerspCamera viewCamera  = PerspCamera(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0), fov, aspect, 0.1f, 10000.0f);

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.1f;
        mat4 rotMat = glm::rotate(glm::radians(gAngle), vec3(0, 1, 0));
        mat4 mvpMat = viewCamera.GetViewProjectionMatrix() * rotMat;

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

                ctx.Reset();

                // Draw grid
                ctx.SetProgram(DxDrawContext::GetStockProgramDrawVertexColor());
                ctx.SetDepthRead(false);
                ctx.SetDepthWrite(false);
                ctx.SetBlendAlpha();
                ctx.SetMatrix(mvpMat);
                ctx.DrawGridXZ(float2(2), 12, 12, 0.65f);

                // Apply rotation to virtual camera
                mat4 virtCamRot  = glm::mat4_cast(gCamRotation);
                auto virtViewDir = (gCamTarget - gCamEyePosition);
                virtViewDir      = virtCamRot * vec4(virtViewDir, 0);
                auto virtTarget  = gCamEyePosition + virtViewDir;
                auto virtUp      = virtCamRot * vec4(vec3(0, 1, 0), 0);

                // Draw virtual camera
                PerspCamera virtCam = PerspCamera(gCamEyePosition, virtTarget, virtUp, gCamFov, gCamAspect, gCamNear, gCamFar);

                // Furstum planes
                Camera::FrustumPlane frLeft, frRight, frTop, frBottom, frNear, frFar;
                virtCam.GetFrustumPlanes(&frLeft, &frRight, &frTop, &frBottom, &frNear, &frFar);
                // Frustum sphere
                auto frSphere = virtCam.GetFrustumSphere();
                // Frustum cone
                auto frCone = virtCam.GetFrustumCone(gFitConeToFarClip);

                ctx.SetDepthRead(false);
                ctx.SetDepthWrite(false);
                ctx.SetBlendAlpha();

                // Normals and perimeter
                ctx.BeginLines();
                {
                    // Plane normals
                    auto DrawPlaneNormal = [&ctx](const Camera::FrustumPlane& plane) {
                        const float kNormalScale = 0.1f;
                        ctx.Color(float4(0, 1, 1, 0.8f));

                        ctx.Vertex(plane.Position);
                        ctx.Vertex(plane.Position + kNormalScale * plane.Normal);
                    };

                    DrawPlaneNormal(frLeft);
                    DrawPlaneNormal(frRight);
                    DrawPlaneNormal(frTop);
                    DrawPlaneNormal(frBottom);
                    DrawPlaneNormal(frNear);
                    DrawPlaneNormal(frFar);

                    // Plane perimeter and cross
                    auto DrawPlanePerimeter = [&ctx](const Camera::FrustumPlane& plane) {
                        ctx.Color(float4(0.5f, 0.8f, 1, 0.4f));
                        ctx.Vertex(plane.C0);
                        ctx.Vertex(plane.C1);
                        ctx.Vertex(plane.C1);
                        ctx.Vertex(plane.C2);
                        ctx.Vertex(plane.C2);
                        ctx.Vertex(plane.C3);
                        ctx.Vertex(plane.C3);
                        ctx.Vertex(plane.C0);

                        ctx.Color(float4(0.5f, 0.8f, 1, 0.15f));
                        ctx.Vertex((plane.C0 + plane.C3) / 2.0f);
                        ctx.Vertex((plane.C1 + plane.C2) / 2.0f);
                        ctx.Vertex((plane.C0 + plane.C1) / 2.0f);
                        ctx.Vertex((plane.C2 + plane.C3) / 2.0f);
                    };

                    DrawPlanePerimeter(frLeft);
                    DrawPlanePerimeter(frRight);
                    DrawPlanePerimeter(frTop);
                    DrawPlanePerimeter(frBottom);
                    DrawPlanePerimeter(frNear);
                    DrawPlanePerimeter(frFar);
                }
                ctx.EndLines();
                // Plane quad
                ctx.BeginTriangles();
                {
                    auto DrawPlaneQuad = [&ctx](const Camera::FrustumPlane& plane) {
                        const float kNormalScale = 0.1f;
                        ctx.Color(float4(0.5f, 0.8f, 1, 0.03f));

                        ctx.Vertex(plane.C0);
                        ctx.Vertex(plane.C1);
                        ctx.Vertex(plane.C2);
                        //
                        ctx.Vertex(plane.C0);
                        ctx.Vertex(plane.C2);
                        ctx.Vertex(plane.C3);
                    };

                    DrawPlaneQuad(frLeft);
                    DrawPlaneQuad(frRight);
                    DrawPlaneQuad(frTop);
                    DrawPlaneQuad(frBottom);
                    DrawPlaneQuad(frNear);
                    DrawPlaneQuad(frFar);
                }
                ctx.EndTriangles();

                std::vector<const Camera::FrustumPlane*> frPlanes = {
                    &frLeft,
                    &frRight,
                    &frTop,
                    &frBottom,
                    &frNear,
                    &frFar,
                };

                auto SignedPointPlaneDistance = [](const float3& P, const float3& planeN, const float3& planeP) -> float {
                    float d = glm::dot(glm::normalize(planeN), P - planeP);
                    return d;
                };

                auto VisibleFrustumPlanes = [&frPlanes, &SignedPointPlaneDistance](const float4& sphere) -> bool {
                    float d0 = SignedPointPlaneDistance(sphere.xyz(), frPlanes[0]->Normal, frPlanes[0]->Position);
                    float d1 = SignedPointPlaneDistance(sphere.xyz(), frPlanes[1]->Normal, frPlanes[1]->Position);
                    float d2 = SignedPointPlaneDistance(sphere.xyz(), frPlanes[2]->Normal, frPlanes[2]->Position);
                    float d3 = SignedPointPlaneDistance(sphere.xyz(), frPlanes[3]->Normal, frPlanes[3]->Position);
                    float d4 = SignedPointPlaneDistance(sphere.xyz(), frPlanes[4]->Normal, frPlanes[4]->Position);
                    float d5 = SignedPointPlaneDistance(sphere.xyz(), frPlanes[5]->Normal, frPlanes[5]->Position);

                    // On positive half space of frustum planes
                    bool pos0 = (d0 >= 0);
                    bool pos1 = (d1 >= 0);
                    bool pos2 = (d2 >= 0);
                    bool pos3 = (d3 >= 0);
                    bool pos4 = (d4 >= 0);
                    bool pos5 = (d5 >= 0);

                    bool inside = pos0 && pos1 && pos2 && pos3 && pos4 && pos5;
                    return inside;
                };

                auto VisibleFrustumSphere = [&frSphere](const float4& sphere) -> bool {
                    // Intersection or inside with frustum sphere
                    bool inside = (glm::distance(sphere.xyz(), frSphere.xyz()) < (sphere.w + frSphere.w));
                    return inside;
                };

                auto VisibleFrustumCone = [&frCone](float4& sphere) -> bool {
                    // Cone and sphere are in possible intersectable range
                    glm::vec3 v0 = sphere.xyz() - frCone.Tip;
                    float     d0 = glm::dot(v0, frCone.Dir);
                    bool      i0 = (d0 <= (frCone.Height + sphere.w));

                    float cs = cos(frCone.Angle * 0.5f);
                    float sn = sin(frCone.Angle * 0.5f);
                    float a  = glm::dot(v0, frCone.Dir);
                    float b  = a * sn / cs;
                    float c  = sqrt(glm::dot(v0, v0) - (a * a));
                    float d  = c - b;
                    float e  = d * cs;
                    bool  i1 = (e < sphere.w);

                    return i0 && i1;
                };

                auto VisibleFrustumConeAndNearPlane = [&frCone, &frNear, &VisibleFrustumCone, &SignedPointPlaneDistance](float4& sphere) -> bool {
                    bool i0 = VisibleFrustumCone(sphere);

                    float d0 = SignedPointPlaneDistance(sphere.xyz(), frNear.Normal, frNear.Position);
                    bool  i1 = (abs(d0) < sphere.w); // Intersects with near plane
                    bool  i2 = (d0 > 0);             // On positive half space of near plane

                    return i0 && (i1 || i2);
                };

                // Draw spheres
                auto sphereMesh = TriMesh::Sphere(1.0f);
                for (auto& sphere : spheres)
                {
                    bool visible = false;
                    switch (gVisibilityFunc)
                    {
                        case VISIBILITY_FUNC_PLANES: visible = VisibleFrustumPlanes(sphere); break;
                        case VISIBILITY_FUNC_SPHERE: visible = VisibleFrustumSphere(sphere); break;
                        case VISIBILITY_FUNC_CONE: visible = VisibleFrustumCone(sphere); break;
                        case VISIBILITY_FUNC_CONE_AND_NEAR_PLANE: visible = VisibleFrustumConeAndNearPlane(sphere); break;
                    }

                    float4 color = float4(0.6f, 0.6f, 0.6f, 0.10f);
                    if (visible)
                    {
                        color = float4(0.1f, 0.8f, 0.2f, 0.25f);
                    }

                    ctx.Color(color);
                    ctx.DrawMesh(sphere.xyz(), float3(sphere.w), sphereMesh);
                }

                if (gVisibilityFunc == VISIBILITY_FUNC_SPHERE)
                {
                    sphereMesh = TriMesh::Sphere(1.0f, 32, 32);

                    ctx.Color(float4(0.7f, 0.7f, 0.2f, 0.025f));
                    ctx.DrawMesh(frSphere.xyz(), float3(frSphere.w), sphereMesh);
                }

                if ((gVisibilityFunc == VISIBILITY_FUNC_CONE) || (gVisibilityFunc == VISIBILITY_FUNC_CONE_AND_NEAR_PLANE))
                {
                    ctx.Color(float4(0.7f, 0.7f, 0.2f, 0.3f));
                    ctx.DrawWireCone(virtCam.GetEyePosition(), virtCam.GetViewDirection(), frCone.Height, frCone.Angle, 32);
                }

                // Flush
                ctx.FlushToCommandList(commandList.Get());

                // ImGui
                window->ImGuiRenderDrawData(renderer.get(), commandList.Get());
            }
            D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            commandList->ResourceBarrier(1, &postRenderBarrier);

            commandList->Close();

            ID3D12CommandList* pList = commandList.Get();
            renderer->Queue->ExecuteCommandLists(1, &pList);

            if (!WaitForGpu(renderer.get()))
            {
                assert(false && "WaitForGpu failed");
                break;
            }
        }

        if (!SwapchainPresent(renderer.get()))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}
