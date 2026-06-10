#pragma once

#include <glm/fwd.hpp>

#include <vulkan/vulkan.h>

namespace TerrainPass {
    IncResult Create();
    void Destroy();
    void Render(VkCommandBuffer cmd);
}
