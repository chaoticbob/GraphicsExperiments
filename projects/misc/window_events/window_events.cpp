#include "window.h"

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;

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
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "002_raytracing_basic_vulkan");
    if (!window) {
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

    while (window->PollEvents()) {
    }

    return 0;
}
