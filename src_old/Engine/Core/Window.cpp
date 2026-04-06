#include "Window.hpp"

namespace Window {
    InferusResult Create(uint32_t Width, uint32_t Height, const std::string &Title, ResizeCallback Callback) {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        glfwWindow = glfwCreateWindow(Width, Height, Title.c_str(), nullptr, nullptr);
        if (!glfwWindow) {
            return InferusResult::FAIL;
        }

        userResizeCallback = Callback;
        glfwSetFramebufferSizeCallback(glfwWindow, StaticFramebufferResizeCallback);

        return InferusResult::SUCCESS;
    }

    void Destroy() {
        glfwDestroyWindow(glfwWindow);
        glfwTerminate();
    }

    std::vector<const char*> GetRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        return std::vector<const char*> (glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    VkResult CreateSurface(VkInstance instance, VkSurfaceKHR &surface) {
        return glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface);
    }

    void GetFramebufferSize(uint32_t &Width, uint32_t &Height) {
        int c_width = 0, c_height = 0;
        glfwGetFramebufferSize(glfwWindow, &c_width, &c_height);
        Width = static_cast<uint32_t>(c_width);
        Height = static_cast<uint32_t>(c_height);
    }

    void StaticFramebufferResizeCallback([[maybe_unused]]GLFWwindow* window, int width, int height) {
        userResizeCallback(width, height);
    }

    void WaitEvents() {
        glfwWaitEvents();
    }

    bool ShouldClose() {
        return glfwWindowShouldClose(glfwWindow);
    }

    void Update() {
        glfwPollEvents();
    }
}
