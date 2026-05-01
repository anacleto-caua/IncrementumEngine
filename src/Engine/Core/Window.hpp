#pragma once

#include <vector>
#include <string>
#include <functional>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include "Types.hpp"

using ResizeCallback = std::function<void(u32 width, u32 height)>;

namespace Window {
    IncResult Create(u32 width, u32 height, const std::string &title, ResizeCallback callback);
    void Destroy();

    std::vector<const char*> GetRequiredExtensions();
    bool CreateSurface(VkInstance instance, VkSurfaceKHR &surface);

    void StaticFramebufferResizeCallback(u32 width, u32 height);
    void GetFramebufferSize(u32 &width, u32 &height);

    void Update();
    bool ShouldClose();
};
