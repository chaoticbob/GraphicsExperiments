#pragma once

#include "config.h"
#include "bitmap.h"

#if !defined(GLFW_INCLUDE_NONE)
#    define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#if defined(__linux__)
#    define GLFW_EXPOSE_NATIVE_X11
#elif defined(__APPLE__)
#    define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(WIN32)
#    define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>

#include <filesystem>
#include <functional>
namespace fs = std::filesystem;

#if defined(ENABLE_IMGUI_D3D12)
#    include "dx_renderer.h"
#    include "backends/imgui_impl_glfw.h"
#    include "backends/imgui_impl_dx12.h"
#endif // defined(ENABLE_IMGUI_D3D12)

#if defined(ENABLE_IMGUI_VULKAN)

#    error "Vulkan ImGui support not available yet"

#    include "vk_renderer.h"
#    include "backends/imgui_impl_glfw.h"
#    include "backends/imgui_impl_vulkan.h"
#endif // defined(ENABLE_IMGUI_VULKAN)

enum MouseButton
{
    MOUSE_BUTTON_LEFT   = 0x1,
    MOUSE_BUTTON_MIDDLE = 0x2,
    MOUSE_BUTTON_RIGHT  = 0x4,
};

class Window
{
private:
    Window(uint32_t width, uint32_t height, const char* pTitle);

public:
    ~Window();

    static std::unique_ptr<Window> Create(uint32_t width, uint32_t height, const char* pTitle);

    uint32_t    GetWidth() const { return mWidth; }
    uint32_t    GetHeight() const { return mHeight; }
    GLFWwindow* GetWindow() const { return mWindow; }

#if defined(WIN32)
    HWND GetHWND() const;
#endif

    bool PollEvents();

    void AddWindowMoveCallbacks(std::function<void(int, int)> fn);
    void AddWindowResizeCallbacks(std::function<void(int, int)> fn);
    void AddMouseDownCallbacks(std::function<void(int, int, int)> fn);
    void AddMouseUpCallbacks(std::function<void(int, int, int)> fn);
    void AddMouseMoveCallbacks(std::function<void(int, int, int)> fn);
    void AddMouseScrollCallbacks(std::function<void(float, float)> fn);
    void AddKeyDownCallbacks(std::function<void(int)> fn);
    void AddKeyUpCallbacks(std::function<void(int)> fn);

    bool IsKeyDown(int key);

#if defined(ENABLE_IMGUI_D3D12)
    bool InitImGuiForD3D12(DxRenderer* pRenderer);
    void ImGuiNewFrameD3D12();
    void ImGuiRenderDrawData(DxRenderer* pRenderer, ID3D12GraphicsCommandList* pCtx);
#endif // defined(ENABLE_IMGUI_D3D12)

#if defined(ENABLE_IMGUI_VULKAN)
    bool InitImGuiForVulkan(VulkanRenderer* pRenderer);
#endif // defined(ENABLE_IMGUI_VULKAN)

private:
    uint32_t    mWidth        = 0;
    uint32_t    mHeight       = 0;
    GLFWwindow* mWindow       = nullptr;
    bool        mImGuiEnabled = false;

private:
    friend struct WindowEvents;

    std::vector<std::function<void(int x, int y)>>                 mWindowMoveCallbacks;
    std::vector<std::function<void(int width, int height)>>        mWindowResizeCallbacks;
    std::vector<std::function<void(int x, int y, int buttons)>>    mMouseDownCallbacks;
    std::vector<std::function<void(int x, int y, int buttons)>>    mMouseUpCallbacks;
    std::vector<std::function<void(int x, int y, int buttons)>>    mMouseMoveCallbacks;
    std::vector<std::function<void(float xoffset, float yoffset)>> mMouseScrollCallbacks;
    std::vector<std::function<void(int key)>>                      mKeyDownCallbacks;
    std::vector<std::function<void(int key)>>                      mKeyUpCallbacks;

    int               mMouseButtons = 0;
    std::vector<bool> mKeyDownState;

    void WindowMoveEvent(int x, int y);
    void WindowResizeEvent(int width, int height);
    void MouseDownEvent(int x, int y, int buttons);
    void MouseUpEvent(int x, int y, int buttons);
    void MouseMoveEvent(int x, int y, int buttons);
    void MouseScrollEvent(float xoffset, float yoffset);
    void KeyDownEvent(int key);
    void KeyUpEvent(int key);
};

fs::path GetExecutablePath();
uint32_t GetProcessId();

std::vector<char> LoadFile(const fs::path& absPath);

const std::vector<fs::path>& GetAssetDirs();
void                         AddAssetDir(const fs::path& absPath);
fs::path                     GetAssetPath(const fs::path& subPath);
std::vector<char>            LoadAsset(const fs::path& subPath);
std::string                  LoadString(const fs::path& subPath);
