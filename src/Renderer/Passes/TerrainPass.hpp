#pragma once

#include <glm/fwd.hpp>

#include <vulkan/vulkan.h>

#include "Renderer/Resources/Image.hpp"

// Resolution and grid scale mirror TerrainManager's VerticesPerEdge/ChunkScale (used only to
// feed the terrain shaders' specialization constants) - HeightScale has no other home, since
// nothing outside rendering needs to know how normalized heightmap values map to world Y.
struct TerrainPassConfig {
    f32 HeightScale = 210.0f;
};

namespace TerrainPass {
    inline TerrainPassConfig Config = {};

    namespace Heightmap {
        inline Image::Id Image;
        constexpr VkFormat Format = VK_FORMAT_R16_UNORM;
    }

    IncResult Create();
    void Destroy();

    void FrameSensibleTransfers();
    void Render();
}
