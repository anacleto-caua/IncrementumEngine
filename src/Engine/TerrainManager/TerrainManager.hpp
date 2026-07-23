#pragma once

#include <array>

#include <glm/fwd.hpp>

#include "TerrainDefinitions.hpp"

namespace TerrainManager {

    using Heightmap = u16[TerrainConfig::Mesh::VerticesPerEdge][TerrainConfig::Mesh::VerticesPerEdge];
    inline u32 CurrentllyActiveChunks = TerrainConfig::Streaming::MaxActiveChunks;

    struct HeightmapStatus {
        glm::ivec2 Position = { 0, 0 };
        bool Ready = false;
    };

    inline std::array<TerrainConfig::Memory::ChunkInstanceData, TerrainConfig::Streaming::MaxActiveChunks> ChunkDrawList;
    inline std::array<HeightmapStatus, TerrainConfig::Streaming::MaxActiveChunks> HeightmapStatus;
    inline std::array<Heightmap, TerrainConfig::Streaming::MaxActiveChunks> HeightmapData;

    // Called by the terrain pass once
    void Init();

    // Called every time the current_player_chunk changes
    void RefreshChunks(glm::ivec2 current_player_chunk);
}
