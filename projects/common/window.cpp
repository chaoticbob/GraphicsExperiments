#include "window.h"

#include <cstring>
#include <cassert>
#include <fstream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// =============================================================================
// Static globals
// =============================================================================
static std::vector<fs::path> sAssetDirs;

// =============================================================================
// WindowEvents
// =============================================================================
struct WindowEvents
{
    static std::unordered_map<GLFWwindow*, GrexWindow*> sWindows;

    static void WindowMoveCallback(GLFWwindow* window, int eventX, int eventY)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        GrexWindow* pAppWindow = it->second;

        pAppWindow->WindowMoveEvent(eventX, eventY);
    }

    static void WindowResizeCallback(GLFWwindow* window, int eventWidth, int eventHeight)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        GrexWindow* pAppWindow = it->second;

        pAppWindow->WindowResizeEvent(eventWidth, eventHeight);
    }

    static void MouseButtonCallback(GLFWwindow* window, int eventButton, int eventAction, int eventMods)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        GrexWindow* pAppWindow = it->second;

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
            pAppWindow->MouseDownEvent(
                static_cast<int>(eventX),
                static_cast<int>(eventY),
                buttons);
        }
        else if (eventAction == GLFW_RELEASE) {
            pAppWindow->MouseUpEvent(
                static_cast<int>(eventX),
                static_cast<int>(eventY),
                buttons);
        }

        // #if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN)
        //         ImGui_ImplGlfw_MouseButtonCallback(window, eventButton, eventAction, eventMods);
        // #endif
    }

    static void MouseMoveCallback(GLFWwindow* window, double eventX, double eventY)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        GrexWindow* pAppWindow = it->second;

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
        pAppWindow->MouseMoveEvent(
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
        GrexWindow* pAppWindow = it->second;

        pAppWindow->MouseScrollEvent(
            static_cast<float>(xoffset),
            static_cast<float>(yoffset));

        // #if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN)
        //         ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
        // #endif
    }

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }
        GrexWindow* pAppWindow = it->second;

        if (action == GLFW_PRESS) {
            pAppWindow->KeyDownEvent(key);
        }
        else if (action == GLFW_RELEASE) {
            pAppWindow->KeyUpEvent(key);
        }

        // #if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN)
        //         ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
        // #endif
    }

    static void CharCallback(GLFWwindow* window, unsigned int c)
    {
        auto it = sWindows.find(window);
        if (it == sWindows.end()) {
            return;
        }

        // #if defined(ENABLE_IMGUI_D3D12) || defined(ENABLE_IMGUI_VULKAN)
        //         ImGui_ImplGlfw_CharCallback(window, c);
        // #endif
    }

    static bool RegisterWindowEvents(GLFWwindow* window, GrexWindow* pAppWindow)
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

        sWindows[window] = pAppWindow;

        return true;
    }
};

std::unordered_map<GLFWwindow*, GrexWindow*> WindowEvents::sWindows;

// =============================================================================
// Window
// =============================================================================
GrexWindow::GrexWindow(uint32_t width, uint32_t height, const char* pTitle)
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

GrexWindow::~GrexWindow()
{
    if (mWindow == nullptr) {
        return;
    }

#if defined(ENABLE_IMGUI_D3D12)
    if (mImGuiEnabled) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
#endif

#if defined(ENABLE_IMGUI_VULKAN)
    if (mImGuiEnabled) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
#endif

#if defined(ENABLE_IMGUI_METAL)
    if (mImGuiEnabled) {
        ImGui_ImplMetal_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
#endif

    glfwDestroyWindow(mWindow);
    mWindow = nullptr;

    glfwTerminate();
}

std::unique_ptr<GrexWindow> GrexWindow::Create(uint32_t width, uint32_t height, const char* pTitle)
{
    GrexWindow* pWindow = new GrexWindow(width, height, pTitle);
    if (IsNull(pWindow)) {
        return nullptr;
    }
    if (IsNull(pWindow->GetWindow())) {
        delete pWindow;
        return nullptr;
    }
    return std::unique_ptr<GrexWindow>(pWindow);
}

void GrexWindow::WindowMoveEvent(int x, int y)
{
    for (auto& callbackFn : mWindowMoveCallbacks) {
        callbackFn(x, y);
    }
}

void GrexWindow::WindowResizeEvent(int width, int height)
{
    for (auto& callbackFn : mWindowResizeCallbacks) {
        callbackFn(width, height);
    }
}

void GrexWindow::MouseDownEvent(int x, int y, int buttons)
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

void GrexWindow::MouseUpEvent(int x, int y, int buttons)
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

void GrexWindow::MouseMoveEvent(int x, int y, int buttons)
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

void GrexWindow::MouseScrollEvent(float xoffset, float yoffset)
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

void GrexWindow::KeyDownEvent(int key)
{
    for (auto& callbackFn : mKeyDownCallbacks) {
        callbackFn(key);
    }

    size_t keyIndex = static_cast<size_t>(key);
    if (keyIndex < mKeyDownState.size()) {
        mKeyDownState[keyIndex] = true;
    }
}

void GrexWindow::KeyUpEvent(int key)
{
    for (auto& callbackFn : mKeyUpCallbacks) {
        callbackFn(key);
    }

    size_t keyIndex = static_cast<size_t>(key);
    if (keyIndex < mKeyDownState.size()) {
        mKeyDownState[keyIndex] = false;
    }
}

void GrexWindow::AddWindowMoveCallbacks(std::function<void(int, int)> fn)
{
    mWindowMoveCallbacks.push_back(fn);
}

void GrexWindow::AddWindowResizeCallbacks(std::function<void(int, int)> fn)
{
    mWindowResizeCallbacks.push_back(fn);
}

void GrexWindow::AddMouseDownCallbacks(std::function<void(int, int, int)> fn)
{
    mMouseDownCallbacks.push_back(fn);
}

void GrexWindow::AddMouseUpCallbacks(std::function<void(int, int, int)> fn)
{
    mMouseUpCallbacks.push_back(fn);
}

void GrexWindow::AddMouseMoveCallbacks(std::function<void(int, int, int)> fn)
{
    mMouseMoveCallbacks.push_back(fn);
}

void GrexWindow::AddMouseScrollCallbacks(std::function<void(float, float)> fn)
{
    mMouseScrollCallbacks.push_back(fn);
}

void GrexWindow::AddKeyDownCallbacks(std::function<void(int)> fn)
{
    mKeyDownCallbacks.push_back(fn);
}

void GrexWindow::AddKeyUpCallbacks(std::function<void(int)> fn)
{
    mKeyUpCallbacks.push_back(fn);
}

bool GrexWindow::IsKeyDown(int key)
{
    size_t keyIndex = static_cast<size_t>(key);
    if (keyIndex < mKeyDownState.size()) {
        return mKeyDownState[keyIndex];
    }
    return false;
}

#if defined(ENABLE_IMGUI_D3D12)
bool GrexWindow::InitImGuiForD3D12(DxRenderer* pRenderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    if (!ImGui_ImplGlfw_InitForOther(mWindow, true)) {
        return false;
    }

    bool res = ImGui_ImplDX12_Init(
        pRenderer->Device.Get(),
        static_cast<int>(pRenderer->SwapchainBufferCount),
        pRenderer->SwapchainRTVFormat,
        nullptr,
        pRenderer->ImGuiFontDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        pRenderer->ImGuiFontDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    if (!res) {
        return false;
    }

    mImGuiEnabled = true;

    return true;
}

void GrexWindow::ImGuiNewFrameD3D12()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GrexWindow::ImGuiRenderDrawData(DxRenderer* pRenderer, ID3D12GraphicsCommandList* pCtx)
{
    ID3D12DescriptorHeap* ppHeaps[1] = {pRenderer->ImGuiFontDescriptorHeap.Get()};
    pCtx->SetDescriptorHeaps(1, ppHeaps);

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCtx);
}

#endif // defined(ENABLE_IMGUI_D3D12)

#if defined(ENABLE_IMGUI_VULKAN)
static void CheckVkResult(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

bool GrexWindow::InitImGuiForVulkan(VulkanRenderer* pRenderer, VkRenderPass renderPass)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    bool res = ImGui_ImplGlfw_InitForVulkan(mWindow, true);
    if (res == false) {
        assert(false && "ImGui init GLFW for Vulkan failed!");
        return false;
    }

    // Create descriptor pool
    {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER,                1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000}
        };

        VkDescriptorPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        createInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        createInfo.maxSets                    = 1000 * IM_ARRAYSIZE(poolSizes);
        createInfo.poolSizeCount              = (uint32_t)IM_ARRAYSIZE(poolSizes);
        createInfo.pPoolSizes                 = poolSizes;

        VkResult vkres = vkCreateDescriptorPool(pRenderer->Device, &createInfo, nullptr, &mDescriptorPool);
        if (vkres != VK_SUCCESS) {
            assert(false && "Create descriptor pool for ImGui failed!");
            return false;
        }
    }

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance                  = pRenderer->Instance;
    initInfo.PhysicalDevice            = pRenderer->PhysicalDevice;
    initInfo.Device                    = pRenderer->Device;
    initInfo.QueueFamily               = pRenderer->GraphicsQueueFamilyIndex;
    initInfo.Queue                     = pRenderer->Queue;
    initInfo.PipelineCache             = VK_NULL_HANDLE;
    initInfo.DescriptorPool            = mDescriptorPool;
    initInfo.Subpass                   = 0;
    initInfo.MinImageCount             = pRenderer->SwapchainImageCount;
    initInfo.ImageCount                = pRenderer->SwapchainImageCount;
    initInfo.MSAASamples               = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator                 = nullptr;
    initInfo.CheckVkResultFn           = CheckVkResult;

    res = ImGui_ImplVulkan_Init(&initInfo, renderPass);
    if (res == false) {
        assert(false && "ImGui init Vulkan failed!");
        return false;
    }

    // Upload Fonts
    {
        VkCommandPool   commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuf  = VK_NULL_HANDLE;
        {
            VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            createInfo.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            createInfo.queueFamilyIndex        = pRenderer->GraphicsQueueFamilyIndex;

            VkResult vkres = vkCreateCommandPool(
                pRenderer->Device,
                &createInfo,
                nullptr,
                &commandPool);
            if (vkres != VK_SUCCESS) {
                assert(false && "vkCreateCommandPool failed!");
                return false;
            }

            VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocInfo.commandPool                 = commandPool;
            allocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount          = 1;

            vkres = vkAllocateCommandBuffers(
                pRenderer->Device,
                &allocInfo,
                &commandBuf);
            if (vkres != VK_SUCCESS) {
                assert(false && "vkAllocateCommandBuffers failed!");
                return false;
            }
        }

        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult vkres = vkBeginCommandBuffer(commandBuf, &beginInfo);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkBeginCommandBuffer failed!");
            return false;
        }

        ImGui_ImplVulkan_CreateFontsTexture(commandBuf);

        vkres = vkEndCommandBuffer(commandBuf);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkEndCommandBuffer failed!");
            return false;
        }

        VkSubmitInfo submitInfo       = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuf;

        vkres = vkQueueSubmit(pRenderer->Queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkQueueSubmit failed!");
            return false;
        }

        vkres = vkQueueWaitIdle(pRenderer->Queue);
        if (vkres != VK_SUCCESS) {
            assert(false && "vkDeviceWaitIdle failed!");
            return false;
        }

        ImGui_ImplVulkan_DestroyFontUploadObjects();

        vkFreeCommandBuffers(pRenderer->Device, commandPool, 1, &commandBuf);
        vkDestroyCommandPool(pRenderer->Device, commandPool, nullptr);
    }

    mImGuiEnabled = true;

    return true;
}

void GrexWindow::ImGuiNewFrameVulkan()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GrexWindow::ImGuiRenderDrawData(VulkanRenderer* pRenderer, VkCommandBuffer cmdBuf)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
}
#endif // defined(ENABLE_IMGUI_VULKAN)

#if defined(ENABLE_IMGUI_METAL)
bool GrexWindow::InitImGuiForMetal(MetalRenderer* pRenderer)
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

void GrexWindow::ImGuiNewFrameMetal(MTL::RenderPassDescriptor* pRenderPassDescriptor)
{
    ImGui_ImplMetal_NewFrame(pRenderPassDescriptor);
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GrexWindow::ImGuiRenderDrawData(MetalRenderer* pRenderer, MTL::CommandBuffer* pCommandBuffer, MTL::RenderCommandEncoder* pRenderEncoder)
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
#if defined(WIN32)
HWND GrexWindow::GetNativeWindowHandle() const
{
    return glfwGetWin32Window(mWindow);
}
#endif

#if defined(__APPLE__)
void* GrexWindow::GetNativeWindowHandle() const
{
   return glfwGetCocoaWindow(mWindow);
}
#endif

bool GrexWindow::PollEvents()
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
#if defined(__linux__)
    pid = static_cast<uint32_t>(getpid());
#elif defined(WIN32)
    pid  = static_cast<uint32_t>(::GetCurrentProcessId());
#elif defined(__APPLE__)
   pid = static_cast<uint32_t>(getpid());
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

    GREX_LOG_INFO("Loaded string from file (LoadString): " << absPath);

    return str;
}

#if defined(GREX_ENABLE_VULKAN)
VkSurfaceKHR GrexWindow::CreateVkSurface(VkInstance instance, const VkAllocationCallbacks* allocator)
{
    VkSurfaceKHR surface;
    VkResult result = glfwCreateWindowSurface(instance, mWindow, allocator, &surface);

    if (result != VK_SUCCESS) {
        GREX_LOG_ERROR("Failed to create VkSurface");
        return VK_NULL_HANDLE;
    }

    return surface;
}
#endif
