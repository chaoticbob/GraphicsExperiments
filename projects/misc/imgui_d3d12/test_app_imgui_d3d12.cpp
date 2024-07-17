#include "window.h"

#include "dx_renderer.h"

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
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

// =============================================================================
// Event functions
// =============================================================================
void WindowMove(int x, int y)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << x << ", " << y << ")";
    GREX_LOG_INFO(ss.str());
}

void WindowResize(int width, int height)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << width << ", " << height << ")";
    GREX_LOG_INFO(ss.str());
}

void MouseDown(int x, int y, int buttons)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << x << ", " << y << ", " << buttons << ")";
    GREX_LOG_INFO(ss.str());
}

void MouseUp(int x, int y, int buttons)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << x << ", " << y << ", " << buttons << ")";
    GREX_LOG_INFO(ss.str());
}

void MouseMove(int x, int y, int buttons)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << x << ", " << y << ", " << buttons << ")";
    GREX_LOG_INFO(ss.str());
}

void MouseScroll(float xoffset, float yoffset)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << xoffset << ", " << yoffset << ")";
    GREX_LOG_INFO(ss.str());
}

void KeyDown(int key)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << key << ")";
    GREX_LOG_INFO(ss.str());
}

void KeyUp(int key)
{
    std::stringstream ss;
    ss << __FUNCTION__ << "(" << key << ")";
    GREX_LOG_INFO(ss.str());
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
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "test_app_imgui_d3d12");
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    window->AddWindowMoveCallbacks(WindowMove);
    window->AddWindowResizeCallbacks(WindowResize);
    window->AddMouseDownCallbacks(MouseDown);
    window->AddMouseUpCallbacks(MouseUp);
    window->AddMouseMoveCallbacks(MouseMove);
    window->AddMouseScrollCallbacks(MouseScroll);
    window->AddKeyDownCallbacks(KeyDown);
    window->AddKeyUpCallbacks(KeyUp);

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
    // Main loop
    // *************************************************************************
    while (window->PollEvents())
    {
        window->ImGuiNewFrameD3D12();

        std::string exePath = GetExecutablePath().filename().string();

        if (ImGui::Begin("Debug Info"))
        {
            ImGui::Columns(2);

            // Exe Path
            {
                ImGui::Text("Exe Path");
                ImGui::NextColumn();
                ImGui::Text("%s", exePath.c_str());
                ImGui::NextColumn();
            }

            // Process ID
            {
                ImGui::Text("PID");
                ImGui::NextColumn();
                ImGui::Text("%d", GetProcessId());
                ImGui::NextColumn();
            }

            // Average FPS
            {
                ImGui::Text("GLFW Time");
                ImGui::NextColumn();
                ImGui::Text("%f sec", static_cast<float>(glfwGetTime()));
                ImGui::NextColumn();
            }
        }
        ImGui::End();

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
                commandList->OMSetRenderTargets(1, &renderer->SwapchainRTVDescriptorHandles[bufferIndex], false, nullptr);

                float clearColor[4] = {0.23f, 0.23f, 0.31f, 0};
                commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 0, nullptr);

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
