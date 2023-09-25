#include "window.h"

#if TARGET_IOS

#import <cassert>
#include <fstream>

#import <UIKit/UIKit.h>
#import <TargetConditionals.h>
#import "ios/AAPLAppDelegate.h"

// =============================================================================
// Static globals
// =============================================================================
static std::vector<fs::path> sAssetDirs;

// =============================================================================
// Window
// =============================================================================
Window::Window(uint32_t width, uint32_t height, const char* pTitle)
    : mWidth(width),
      mHeight(height)
{
   int argc = 0;
   char* argv[] = {};
   UIApplicationMain(argc, argv,  nil, NSStringFromClass([AAPLAppDelegate class]));
}

Window::~Window()
{
#if defined(ENABLE_IMGUI_METAL)
    if (mImGuiEnabled) {
        ImGui_ImplMetal_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
#endif
}

std::unique_ptr<Window> Window::Create(uint32_t width, uint32_t height, const char* pTitle)
{
    Window* pWindow = new Window(width, height, pTitle);
    if (IsNull(pWindow)) {
        return nullptr;
    }
    return std::unique_ptr<Window>(pWindow);
}

void Window::WindowMoveEvent(int x, int y)
{
    for (auto& callbackFn : mWindowMoveCallbacks) {
        callbackFn(x, y);
    }
}

void Window::WindowResizeEvent(int width, int height)
{
    for (auto& callbackFn : mWindowResizeCallbacks) {
        callbackFn(width, height);
    }
}

void Window::MouseDownEvent(int x, int y, int buttons)
{
#if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN) || defined(ENABLE_IMGUI_METAL)
    if (mImGuiEnabled) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }
#endif

    for (auto& callbackFn : mMouseDownCallbacks) {
        callbackFn(x, y, buttons);
    }
}

void Window::MouseUpEvent(int x, int y, int buttons)
{
#if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN) || defined(ENABLE_IMGUI_METAL)
    if (mImGuiEnabled) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }
#endif

    for (auto& callbackFn : mMouseUpCallbacks) {
        callbackFn(x, y, buttons);
    }
}

void Window::MouseMoveEvent(int x, int y, int buttons)
{
#if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN) || defined(ENABLE_IMGUI_METAL)
    if (mImGuiEnabled) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }
#endif

    for (auto& callbackFn : mMouseMoveCallbacks) {
        callbackFn(x, y, buttons);
    }
}

void Window::MouseScrollEvent(float xoffset, float yoffset)
{
#if defined(ENABLE_IMGUI_D3D12)
    if (mImGuiEnabled) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }
#endif

    for (auto& callbackFn : mMouseScrollCallbacks) {
        callbackFn(xoffset, yoffset);
    }
}

void Window::KeyDownEvent(int key)
{
    for (auto& callbackFn : mKeyDownCallbacks) {
        callbackFn(key);
    }

    size_t keyIndex = static_cast<size_t>(key);
    if (keyIndex < mKeyDownState.size()) {
        mKeyDownState[keyIndex] = true;
    }
}

void Window::KeyUpEvent(int key)
{
    for (auto& callbackFn : mKeyUpCallbacks) {
        callbackFn(key);
    }

    size_t keyIndex = static_cast<size_t>(key);
    if (keyIndex < mKeyDownState.size()) {
        mKeyDownState[keyIndex] = false;
    }
}

void Window::AddWindowMoveCallbacks(std::function<void(int, int)> fn)
{
    mWindowMoveCallbacks.push_back(fn);
}

void Window::AddWindowResizeCallbacks(std::function<void(int, int)> fn)
{
    mWindowResizeCallbacks.push_back(fn);
}

void Window::AddMouseDownCallbacks(std::function<void(int, int, int)> fn)
{
    mMouseDownCallbacks.push_back(fn);
}

void Window::AddMouseUpCallbacks(std::function<void(int, int, int)> fn)
{
    mMouseUpCallbacks.push_back(fn);
}

void Window::AddMouseMoveCallbacks(std::function<void(int, int, int)> fn)
{
    mMouseMoveCallbacks.push_back(fn);
}

void Window::AddMouseScrollCallbacks(std::function<void(float, float)> fn)
{
    mMouseScrollCallbacks.push_back(fn);
}

void Window::AddKeyDownCallbacks(std::function<void(int)> fn)
{
    mKeyDownCallbacks.push_back(fn);
}

void Window::AddKeyUpCallbacks(std::function<void(int)> fn)
{
    mKeyUpCallbacks.push_back(fn);
}

bool Window::IsKeyDown(int key)
{
    size_t keyIndex = static_cast<size_t>(key);
    if (keyIndex < mKeyDownState.size()) {
        return mKeyDownState[keyIndex];
    }
    return false;
}

#if defined(ENABLE_IMGUI_METAL)
bool Window::InitImGuiForMetal(MetalRenderer* pRenderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
   
   ImGuiIO& io = ImGui::GetIO();
   io.DisplayFramebufferScale = ImVec2(1, 1);
   
   bool res = ImGui_ImplGlfw_InitForOther(mWindow, true);
   if (res == false) {
       assert(false && "ImGui init GLFW for Metal failed!");
       return false;
   }

    res = ImGui_ImplMetal_Init(pRenderer->Device.get());
    if (res == false) {
        assert(false && "ImGui init Metal failed!");
        return false;
    }

    mImGuiEnabled = ImGui_ImplMetal_CreateFontsTexture(pRenderer->Device.get());
    mImGuiEnabled = mImGuiEnabled && ImGui_ImplMetal_CreateDeviceObjects(pRenderer->Device.get());

    return mImGuiEnabled;
}

void Window::ImGuiNewFrameMetal(MTL::RenderPassDescriptor* pRenderPassDescriptor)
{
    ImGui_ImplMetal_NewFrame(pRenderPassDescriptor);
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Window::ImGuiRenderDrawData(MetalRenderer* pRenderer, MTL::CommandBuffer* pCommandBuffer, MTL::RenderCommandEncoder* pRenderEncoder)
{
    ImGui::Render();
   
   // It seems like we're missing something when it comes to High DPI rendering
   // When the system is not using the full resolution of the monitor (e.g. 2560x1440 on a 4k monitor)
   // the view.window.screen.backingScaleFactor seems to always be set to 2.0. ImGUI uses that factor
   // to scale it's scissor which causes the following debug validation layer error
   //
   // -[MTLDebugRenderCommandEncoder setScissorRect:]:3814: failed assertion `Set Scissor Rect Validation
   //    (rect.x(0) + rect.width(3840))(3840) must be <= render pass width(1920)
   //    (rect.y(0) + rect.height(2160))(2160) must be <= render pass height(1080)
   //
   // So to avoid that I'm setting the FramebufferScale to 1.0 regardless of what the system says.
   ImDrawData* drawData = ImGui::GetDrawData();
   drawData->FramebufferScale = ImVec2(1,1);
   
   ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), pCommandBuffer, pRenderEncoder);
}
#endif // defined(ENABLE_IMGUI_METAL)

// =============================================================================
// Platform functions
// =============================================================================

bool Window::PollEvents()
{
   bool shouldClose = false;
    if (shouldClose) {
        return false;
    }

    return true;
}

fs::path GetExecutablePath()
{
    fs::path path;
#if defined(TARGET_IOS)
   // NOCHECKIN
   path = fs::path("/Users/loos/code/GraphicsExperiments/bin/Debug/");
#elif defined(__APPLE__)
   char buf[PATH_MAX];
   uint32_t size = sizeof(buf);
   std::memset(buf, 0, size);
   _NSGetExecutablePath(buf, &size);
   path = fs::path(buf);
#else
#    error "unsupported platform"
#endif
    return path;
}

uint32_t GetProcessId()
{
    uint32_t pid = UINT32_MAX;
#if defined(TARGET_IOS)
   pid = 0; // NOCHECKIN
#elif defined(__APPLE__)
   pid = static_cast<uint32_t>(getpid());
#else
#  error "unsupported_platform"
#endif
    return pid;
}

std::vector<char> LoadFile(const fs::path& absPath)
{
    if (!fs::exists(absPath)) {
        return {};
    }

    size_t size = fs::file_size(absPath);
    if (size == 0) {
        return {};
    }

    std::ifstream is(absPath.c_str(), std::ios::binary);
    if (!is.is_open()) {
        return {};
    }

    std::vector<char> buffer(size);
    is.read(buffer.data(), size);

    return buffer;
}

static void InitAssetDirs()
{
    if (!sAssetDirs.empty()) {
        return;
    }

    auto       dir  = GetExecutablePath().parent_path();
    const auto root = dir.root_path();

    while (true) {
        auto assetDir = dir / "assets";
        sAssetDirs.push_back(assetDir);
        GREX_LOG_INFO("Adding asset directory: " << assetDir);
        if (dir == root) {
            break;
        }
        dir = dir.parent_path();
    }

    dir = GetExecutablePath().parent_path();
    while (true) {
        auto assetDir = dir / "__local_assets__";
        if (fs::exists(assetDir)) {
            sAssetDirs.push_back(assetDir);
            GREX_LOG_INFO("Adding asset directory: " << assetDir);
        }
        if (dir == root) {
            break;
        }
        dir = dir.parent_path();
    }
}

const std::vector<fs::path>& GetAssetDirs()
{
    InitAssetDirs();
    return sAssetDirs;
}

void AddAssetDir(const fs::path& absPath)
{
    InitAssetDirs();
    if (fs::exists(absPath)) {
        sAssetDirs.push_back(absPath);
    }
}

fs::path GetAssetPath(const fs::path& subPath)
{
    InitAssetDirs();
    fs::path assetPath;
    for (auto& assetDir : sAssetDirs) {
        fs::path path = assetDir / subPath;
        if (fs::exists(path)) {
            assetPath = path;
            break;
        }
    }
    return assetPath;
}

std::vector<fs::path> GetEveryAssetPath(const fs::path& subPath)
{
    InitAssetDirs();
    std::vector<fs::path> assetPaths;
    for (auto& assetDir : sAssetDirs) {
        fs::path path = assetDir / subPath;
        if (fs::exists(path)) {
            assetPaths.push_back(path);
        }
    }
    return assetPaths;
}

std::vector<char> LoadAsset(const fs::path& subPath)
{
    fs::path absPath = GetAssetPath(subPath);
    return LoadFile(absPath);
}

std::string LoadString(const fs::path& subPath)
{
    fs::path absPath = GetAssetPath(subPath);
    if (!fs::exists(absPath)) {
        return {};
    }

    size_t size = fs::file_size(absPath);
    if (size == 0) {
        return {};
    }

    std::ifstream is(absPath.c_str(), std::ios::binary);
    if (!is.is_open()) {
        return {};
    }

    std::string str(size, 0);
    is.read(str.data(), size);

    return str;
}

#endif // TARGET_IOS
