#pragma once

#include <glm/fwd.hpp>
#include <glm/ext.hpp>
#include <vulkan/vulkan.h>

#include "Engine/Types.hpp"
#include "Engine/Core/Camera.hpp"
#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"

struct TerrainDescriptorSet {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

struct TerrainPushConstants {
    glm::mat4 CameraMVP;
    glm::vec4 PlayerPosition;
};

namespace TerrainRenderer {
    InferusResult Create(BufferSystem::Id &CreationWiseStagingBufer);
    void FeedTerrainSystemPointers();
    void BindCamera(Camera::Camera3D &Camera);

    void Destroy();

    void Render(VkCommandBuffer cmd);

}
