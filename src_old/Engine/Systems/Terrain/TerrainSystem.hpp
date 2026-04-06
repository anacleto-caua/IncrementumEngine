#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "Engine/Systems/Terrain/TerrainTypes.hpp"
#include "Engine/InferusRenderer/Image/ImageSystem.hpp"
#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"

namespace TerrainSystem {
    void Create(glm::vec3* PlayerPos);
    void Destroy();

    void Update();

    void FeedTerrainRenderer(ChunkHeightmapLink* ChunkLinkMap, uint16_t* HeightmapMap);
    void FeedTerrainData(BufferSystem::Id ChunkLinkId, ImageSystem::Id HeightmapId);

    void FullWriteChunkData();
};
