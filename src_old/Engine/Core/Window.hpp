#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "Engine/Types.hpp"

using ResizeCallback = std::function<void(uint32_t Width, uint32_t Height)>;

namespace Window {
    inline GLFWwindow *glfwWindow = nullptr;
    inline ResizeCallback userResizeCallback = nullptr;

    InferusResult Create(uint32_t Width, uint32_t Height, const std::string &Title, ResizeCallback OwnerCallback);
    void Destroy();

    std::vector<const char*> GetRequiredExtensions();
    VkResult CreateSurface(VkInstance instance, VkSurfaceKHR &surface);

    void StaticFramebufferResizeCallback(GLFWwindow* window, int width, int height);
    void GetFramebufferSize(uint32_t &width, uint32_t &height);

    void WaitEvents();
    void Update();
    bool ShouldClose();
};
