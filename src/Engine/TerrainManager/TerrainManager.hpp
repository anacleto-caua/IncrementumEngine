#pragma once

#include <glm/fwd.hpp>

namespace TerrainManager {

    inline u32 CurrentllyActiveChunks = 0;

    // Called by the terrain pass once
    void Init();

    // Called every time the current_player_chunk changes
    void RefreshChunks(glm::ivec2 current_player_chunk);
}
