#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "Engine/Systems/Terrain/TerrainTypes.hpp"

namespace TerrainSystem {
    void Create(glm::vec3* PlayerPos);
    void Destroy();

    void Update();

    void FeedTerrainRenderer(ChunkHeightmapLink* ChunkLinkMap, uint16_t* HeightmapMap);
    void FullWriteChunkData();
};
