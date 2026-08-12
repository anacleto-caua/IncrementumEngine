#pragma once

#include <glm/fwd.hpp>

#include <vulkan/vulkan.h>

struct TerrainPassConfig {
    u32 Resolution = 64;
    f32 GridScale =  50.0f;
    f32 HeightScale = 210.0f;
};

namespace TerrainPass {
    inline TerrainPassConfig Config = {};

    IncResult Create();
    void Destroy();

    void FrameSensibleTransfers();
    void Render();
}
