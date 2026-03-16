#pragma once

#include <glm/fwd.hpp>
#include <glm/ext.hpp>
#include <vulkan/vulkan.h>

#include "Engine/Types.hpp"
#include "Engine/Core/Camera.hpp"
#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"

namespace TerrainRenderer {
    InferusResult Create(BufferSystem::Id &CreationWiseStagingBufer);
    void FeedTerrainSystemPointers();
    void BindCamera(Camera::Camera3D &Camera);

    void Destroy();

    void Render(VkCommandBuffer cmd);

}
