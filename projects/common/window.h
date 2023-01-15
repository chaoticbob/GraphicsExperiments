#pragma once

#include "config.h"

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

private:
    uint32_t    mWidth  = 0;
    uint32_t    mHeight = 0;
    GLFWwindow* mWindow = nullptr;
};