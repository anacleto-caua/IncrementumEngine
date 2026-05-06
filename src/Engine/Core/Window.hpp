#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace Window {
    std::vector<const char*> GetRequiredExtensions();
    bool CreateSurface(VkInstance instance, VkSurfaceKHR &surface);
    void GetFramebufferSize(i32 &width, i32 &height);
};
