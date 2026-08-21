#pragma once

#include <array>

#include "Core/Math.hpp"
#include "Core/Frustum.hpp"

/**
 * A lot of the constants here should match something in the terrain shaders,
 * so keep an eye out for it since there's no reflection.
 */
namespace TerrainManager {
    // --- Mesh / world scale ---
    constexpr u32 VerticesPerEdge = 64;
    constexpr f64 ChunkScale = 100.0;

    // --- Streaming / cache sizing ---
    constexpr u32 ExplorationRadius = 10;

    // How many chunks are actually drawn each frame (chunks within ExplorationRadius of the player)
    constexpr u32 MaxDrawnChunks = []() {
        u32 quadrant_points = 0;
        u32 radius = ExplorationRadius;
        u32 y = radius;

        for (u32 x = 1; x <= radius; x++) {
            while (x * x + y * y > radius * radius) {
                y--;
            }
            quadrant_points += y;
        }
        return 1 + 4 * radius + 4 * quadrant_points;
    }();

    // How many chunks stay resident on the GPU at once (>= MaxDrawnChunks). The extra headroom
    // is a cache: chunks that fall out of the drawn set keep their generated data here instead
    // of being evicted immediately, so revisiting them is instant.
    constexpr u32 MaxCachedChunks = MaxDrawnChunks * 2;

    static_assert(
        MaxCachedChunks >= MaxDrawnChunks,
        "MaxCachedChunks must be at least MaxDrawnChunks, or LRU eviction could evict a chunk "
        "that's still part of the current frame's drawn set"
    );

    using Heightmap = u16[VerticesPerEdge][VerticesPerEdge];

    // Per-drawn-chunk instance data uploaded to the GPU - mirrors the shader's ChunkDrawData struct.
    struct ChunkInstanceData {
        ivec2 WorldPos;
        u32 TextureLayer;
        u32 padding;
    };

    // How many of ChunkDrawList's entries are actually populated this frame - dynamic, since a
    // chunk that's still generating simply isn't in the drawn set yet.
    inline u32 CurrentlyActiveChunks = 0;

    // One entry per GPU-resident heightmap slot (MaxCachedChunks of them - more than are ever
    // drawn at once, so a chunk that falls out of the drawn set keeps its slot until the cache
    // itself needs the room).
    struct CacheSlot {
        ivec2 Position = { 0, 0 };
        bool Valid = false;       // has this slot ever actually been generated into
        u64 LastUsedTick = 0;     // last RefreshChunks() call this slot was part of the drawn set
    };

    inline std::array<ChunkInstanceData, MaxDrawnChunks> ChunkDrawList;
    inline std::array<CacheSlot, MaxCachedChunks> Cache;
    inline std::array<Heightmap, MaxCachedChunks> HeightmapData;

    // Debug/validation toggle - lets frustum culling be disabled at runtime to sanity-check its
    // effect (e.g. via ImGui) without a rebuild.
    inline bool CullingEnabled = true;

    // Called by the terrain pass once
    void Init();

    // Called every time the current_player_chunk changes
    void RefreshChunks(vec3 player_position, const Frustum& camera_frustum);
}
