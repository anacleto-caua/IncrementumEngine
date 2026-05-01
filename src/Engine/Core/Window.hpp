#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace Window {
    std::vector<const char*> GetRequiredExtensions();
    bool CreateSurface(VkInstance instance, VkSurfaceKHR &surface);
    void GetFramebufferSize(u32 &width, u32 &height);
};
