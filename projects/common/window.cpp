#include "window.h"

#include <cassert>

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
        assert(false);
        return;
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