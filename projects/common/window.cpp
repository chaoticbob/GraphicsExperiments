#include "window.h"

#include <cassert>
#include <fstream>

// =============================================================================
// Static globals
// =============================================================================
static std::vector<fs::path> sAssetDirs;

// =============================================================================
// WindowEvents
// =============================================================================
struct WindowEvents
{
    static std::unordered_map<GLFWwindow*, Window*> sWindows;

    static void WindowMoveCallback(GLFWwindow* window, int eventX, int eventY)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        pWindow->WindowMoveEvent(eventX, eventY);
    }

    static void WindowResizeCallback(GLFWwindow* window, int eventWidth, int eventHeight)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        pWindow->WindowResizeEvent(eventWidth, eventHeight);
    }

    static void MouseButtonCallback(GLFWwindow* window, int eventButton, int eventAction, int eventMods)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        int buttons = 0;
        if (eventButton == GLFW_MOUSE_BUTTON_LEFT) {
            buttons |= MOUSE_BUTTON_LEFT;
        }
        if (eventButton == GLFW_MOUSE_BUTTON_RIGHT) {
            buttons |= MOUSE_BUTTON_RIGHT;
        }
        if (eventButton == GLFW_MOUSE_BUTTON_MIDDLE) {
            buttons |= MOUSE_BUTTON_MIDDLE;
        }

        double eventX;
        double eventY;
        glfwGetCursorPos(window, &eventX, &eventY);

        if (eventAction == GLFW_PRESS) {
            pWindow->MouseDownEvent(
                static_cast<int>(eventX),
                static_cast<int>(eventY),
                buttons);
        }
        else if (eventAction == GLFW_RELEASE) {
            pWindow->MouseUpEvent(
                static_cast<int>(eventX),
                static_cast<int>(eventY),
                buttons);
        }

        // if (pWindow->GetSettings()->enableImGui) {
        //     ImGui_ImplGlfw_MouseButtonCallback(window, event_button, event_action, event_mods);
        // }
    }

    static void MouseMoveCallback(GLFWwindow* window, double eventX, double eventY)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        uint32_t buttons = 0;
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            buttons |= MOUSE_BUTTON_LEFT;
        }
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            buttons |= MOUSE_BUTTON_RIGHT;
        }
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            buttons |= MOUSE_BUTTON_MIDDLE;
        }
        pWindow->MouseMoveEvent(
            static_cast<int>(eventX),
            static_cast<int>(eventY),
            buttons);
    }

    static void MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        pWindow->MouseScrollEvent(
            static_cast<float>(xoffset),
            static_cast<float>(yoffset));

        // if (pWindow->GetSettings()->enableImGui) {
        //     ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
        // }
    }

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        if (action == GLFW_PRESS) {
            pWindow->KeyDownEvent(key);
        }
        else if (action == GLFW_RELEASE) {
            pWindow->KeyUpEvent(key);
        }

        // if (pWindow->GetSettings()->enableImGui) {
        //     ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
        // }
    }

    static void CharCallback(GLFWwindow* window, unsigned int c)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        Window* pWindow = it->second;

        // if (pWindow->GetSettings()->enableImGui) {
        //     ImGui_ImplGlfw_CharCallback(window, c);
        // }
    }

    static bool RegisterWindowEvents(GLFWwindow* window, Window* pWindow)
    {
        auto it = sWindows.find(window);
        if (it != sWindows.end()) {
            return false;
        }

        glfwSetWindowPosCallback(window, WindowEvents::WindowMoveCallback);
        glfwSetWindowSizeCallback(window, WindowEvents::WindowResizeCallback);
        glfwSetMouseButtonCallback(window, WindowEvents::MouseButtonCallback);
        glfwSetCursorPosCallback(window, WindowEvents::MouseMoveCallback);
        glfwSetScrollCallback(window, WindowEvents::MouseScrollCallback);
        glfwSetKeyCallback(window, WindowEvents::KeyCallback);
        glfwSetCharCallback(window, WindowEvents::CharCallback);

        sWindows[window] = pWindow;

        return true;
    }
};

std::unordered_map<GLFWwindow*, Window*> WindowEvents::sWindows;

// =============================================================================
// Window
// =============================================================================
Window::Window(uint32_t width, uint32_t height, const char* pTitle)
    : mWidth(width),
      mHeight(height)
{
    if (glfwInit() != GLFW_TRUE) {
        assert(false && "glfwInit failed");
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    mWindow = glfwCreateWindow(static_cast<int>(mWidth), static_cast<int>(mHeight), pTitle, nullptr, nullptr);
    if (!mWindow) {
        assert(false && "glfwCreateWindow failed");
        return;
    }

    if (!WindowEvents::RegisterWindowEvents(mWindow, this)) {
        assert(false && "WindowEvents::RegisterWindowEvents failed");
        return;
    }

    for (size_t i = 0; i < static_cast<size_t>(GLFW_KEY_LAST); ++i) {
        mKeyDownState.push_back(false);
    }
}

Window::~Window()
{
    if (mWindow == nullptr) {
        return;
    }

    glfwDestroyWindow(mWindow);
    mWindow = nullptr;

    glfwTerminate();
}

std::unique_ptr<Window> Window::Create(uint32_t width, uint32_t height, const char* pTitle)
{
    Window* pWindow = new Window(width, height, pTitle);
    if (IsNull(pWindow)) {
        return nullptr;
    }
    if (IsNull(pWindow->GetWindow())) {
        delete pWindow;
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
    for (auto& callbackFn : mMouseDownCallbacks) {
        callbackFn(x, y, buttons);
    }
}

void Window::MouseUpEvent(int x, int y, int buttons)
{
    for (auto& callbackFn : mMouseUpCallbacks) {
        callbackFn(x, y, buttons);
    }
}

void Window::MouseMoveEvent(int x, int y, int buttons)
{
    for (auto& callbackFn : mMouseMoveCallbacks) {
        callbackFn(x, y, buttons);
    }
}

void Window::MouseScrollEvent(float xoffset, float yoffset)
{
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

// =============================================================================
// Platform functions
// =============================================================================
#if defined(WIN32)
HWND Window::GetHWND() const
{
    return glfwGetWin32Window(mWindow);
}
#endif

bool Window::PollEvents()
{
    bool shouldClose = (glfwWindowShouldClose(mWindow) != 0);
    if (shouldClose) {
        return false;
    }

    glfwPollEvents();

    return true;
}

fs::path GetExecutablePath()
{
    fs::path path;
#if defined(__linux__)
    char buf[PATH_MAX];
    std::memset(buf, 0, PATH_MAX);
    readlink("/proc/self/exe", buf, PATH_MAX);
    path = fs::path(buf);
#elif defined(WIN32)
    HMODULE this_win32_module = GetModuleHandleA(nullptr);
    char    buf[MAX_PATH];
    std::memset(buf, 0, MAX_PATH);
    GetModuleFileNameA(this_win32_module, buf, MAX_PATH);
    path = fs::path(buf);
#else
#    error "unsupported platform"
#endif
    return path;
}

std::vector<char> LoadFile(const fs::path& absPath)
{
    std::ifstream is(absPath.c_str(), std::ios::binary);
    if (!is.is_open()) {
        return {};
    }

    size_t size = fs::file_size(absPath);
    if (size == 0) {
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

    auto dir  = GetExecutablePath().parent_path();
    auto root = dir.root_path();

    while (true) {
        auto assetDir = dir / "assets";
        sAssetDirs.push_back(assetDir);
        GREX_LOG_INFO("Adding asset directory: " << assetDir);
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

std::vector<char> LoadAsset(const fs::path& subPath)
{
    fs::path absPath = GetAssetPath(subPath);
    return LoadFile(absPath);
}