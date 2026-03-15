#pragma once

#include <vulkan/vulkan.h>

#include "Engine/Types.hpp"

namespace ImGuiRenderer {
    InferusResult Create();
    void Destroy();

    void Render(VkCommandBuffer cmd);
};
