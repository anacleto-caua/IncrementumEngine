#pragma once

#include <glm/fwd.hpp>
#include <glm/ext.hpp>
#include <vulkan/vulkan.h>

#include "Engine/Types.hpp"
#include "Engine/Core/Camera.hpp"

namespace TerrainRenderer {
    InferusResult Create();
    void FeedTerrainSystemPointers();
    void BindCamera(Camera::Camera3D &Camera);

    void Destroy();

    void Render(VkCommandBuffer cmd);

}
