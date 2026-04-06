#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "Engine/Types.hpp"

namespace Renderer {
    // Swapchain
    inline VkSurfaceCapabilitiesKHR SurfaceCapabilities {};
    inline uint32_t SwapchainImageCount = 0;

    InferusResult Create();
    void Destroy();

    void Render();

    void Resize(uint32_t Width, uint32_t Height);

};
