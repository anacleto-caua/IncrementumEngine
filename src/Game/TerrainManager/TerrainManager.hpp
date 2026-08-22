#pragma once

#include <array>
#include <chrono>
#include <unordered_map>

#include "Core/Frustum.hpp"
#include "Core/Math.hpp"

/**
 * A lot of the constants here should match something in the terrain shaders,
 * so keep an eye out for it since there's no reflection.
 */
namespace TerrainManager {
    // --- Mesh / world scale ---
    constexpr u32 VerticesPerEdge = 64;

    // --- LOD rings ---
    // Every ring past ring 0 shares one ExplorationRadius and only its ChunkScale differs, so
    // they're a fixed-size array of one shared type rather than four hand-written near-duplicates.
    constexpr f64 ChunkScale = 100.0;
    constexpr u32 ExplorationRadius = 10;

    constexpr u32 OuterRingCount = 4;
    constexpr u32 OuterRingExplorationRadius = 5;
    // ChunkScale doubles every ring (200, 400, 800, 1600) so the triangle-size-to-viewing-distance
    // ratio stays identical at every ring's outer edge.
    constexpr std::array<f64, OuterRingCount> OuterRingChunkScales = { 200.0, 400.0, 800.0, 1600.0 };

    // Quadrant-symmetric lattice-point count within a chunk-count radius - shared by ring 0
    // (radius = ExplorationRadius) and every outer ring (radius = OuterRingExplorationRadius).
    constexpr u32 ComputeMaxDrawnChunks(u32 radius) {
        u32 quadrant_points = 0;
        u32 y = radius;

        for (u32 x = 1; x <= radius; x++) {
            while (x * x + y * y > radius * radius) {
                y--;
            }
            quadrant_points += y;
        }
        return 1 + 4 * radius + 4 * quadrant_points;
    }

    // How many chunks are actually drawn each frame, and how many stay resident on the GPU at
    // once (>= drawn - the extra headroom is a cache: chunks that fall out of the drawn set keep
    // their generated data here instead of being evicted immediately, so revisiting them is
    // instant) - ring 0.
    constexpr u32 MaxDrawnChunks = ComputeMaxDrawnChunks(ExplorationRadius);
    constexpr u32 MaxCachedChunks = MaxDrawnChunks * 2;

    // An outer ring's own candidate scan has to reach its OUTER radius, not just
    // OuterRingExplorationRadius (which is the ring's own ANNULUS WIDTH in its own chunk-grid
    // units, not a from-the-player scan radius the way ring 0's ExplorationRadius is). Since
    // ChunkScale and cumulative radius both double every ring, an outer ring's own outer radius in
    // its own chunk-grid units is always exactly double its inner radius there.
    constexpr u32 OuterRingScanRadius = OuterRingExplorationRadius * 2;

    // Sized for the actual annulus (candidates within OuterRingScanRadius but outside
    // OuterRingExplorationRadius) - a full circle of radius OuterRingExplorationRadius undercounts
    // by ~3x and starves streaming.
    constexpr u32 OuterRingMaxDrawnChunks = ComputeMaxDrawnChunks(OuterRingScanRadius) - ComputeMaxDrawnChunks(OuterRingExplorationRadius);

    // Outer rings use a smaller, fixed additive cache headroom instead of ring 0's *2: an outer
    // ring's candidate set only shifts when the player crosses one of ITS chunks (up to 1600 world
    // units for the outermost ring), so LRU eviction pressure there is far lower than ring 0's.
    constexpr u32 OuterRingCacheHeadroom = 64;
    constexpr u32 OuterRingMaxCachedChunks = OuterRingMaxDrawnChunks + OuterRingCacheHeadroom;

    static_assert(
        MaxCachedChunks >= MaxDrawnChunks && OuterRingMaxCachedChunks >= OuterRingMaxDrawnChunks,
        "MaxCachedChunks must be at least MaxDrawnChunks for every ring, or LRU eviction could "
        "evict a chunk that's still part of the current frame's drawn set"
    );

    // Every visible-chunk slot across every ring, summed - sizes the shared heightmap texture
    // array and the shared per-frame chunk-instance SSBO.
    constexpr u32 TotalMaxDrawnChunks = MaxDrawnChunks + OuterRingCount * OuterRingMaxDrawnChunks;
    constexpr u32 TotalMaxCachedChunks = MaxCachedChunks + OuterRingCount * OuterRingMaxCachedChunks;

    // TotalMaxCachedChunks is the heightmap array's ArrayLayers count (TerrainPass.cpp) - Vulkan
    // caps this per-format (VK_FORMAT_R16_UNORM measured at 2048 on tested hardware). 1900 leaves
    // margin below that for less capable hardware - if this fires, reduce
    // OuterRingCount/OuterRingCacheHeadroom or query the real device limit at runtime.
    static_assert(
        TotalMaxCachedChunks <= 1900,
        "TotalMaxCachedChunks is close to or past Vulkan's per-format maxArrayLayers on typical "
        "hardware - reduce OuterRingCount/OuterRingCacheHeadroom or make the heightmap array size "
        "a runtime device-queried value instead of this compile-time constant"
    );

    // The world-space radius the whole LOD ring system streams/draws out to (sum of every ring's
    // own span). Camera::FarPlane must reach at least this far, or terrain past ring 0's own edge
    // gets clipped by the projection matrix and CPU-side frustum culling regardless of how well
    // streaming itself works.
    constexpr f64 ComputeTotalCoverageRadius() {
        f64 radius = ChunkScale * static_cast<f64>(ExplorationRadius);
        for (u32 i = 0; i < OuterRingCount; i++) {
            radius += OuterRingChunkScales[i] * static_cast<f64>(OuterRingExplorationRadius);
        }
        return radius;
    }
    constexpr f64 TotalCoverageRadius = ComputeTotalCoverageRadius();

    using Heightmap = u16[VerticesPerEdge][VerticesPerEdge];

    // --- Props ---
    // Which mesh a PropInstance uses - lives here (Game layer) rather than under Renderer/ since
    // TerrainManager is what produces placement data and stamps this index into it; PropPass
    // (Renderer layer) just interprets it as a table index, the same direction TerrainPass already
    // depends on TerrainManager's types.
    enum class PropModel : u32 {
        Tree,
        Rock,

        _COUNT_
    };

    // Placeholder, not a considered value - retune once real prop art/density targets exist.
    constexpr u32 MaxPropsPerChunk = 24;

    struct PropInstance {
        vec3 WorldPosition;
        f32 YRotation = 0.0f;  // radians, Y-axis only - props don't need full 3-axis rotation
        f32 Scale = 1.0f;
        PropModel Model = PropModel::Tree;
    };

    struct PropPlacement {
        std::array<PropInstance, MaxPropsPerChunk> Instances;
        u32 Count = 0;
    };

    // Per-drawn-chunk instance data uploaded to the GPU - mirrors the shader's ChunkDrawData struct.
    struct ChunkInstanceData {
        ivec2 WorldPos;
        u32 TextureLayer;
        f32 Scale;   // this chunk's ring's ChunkScale
    };

    // How many of ChunkDrawList's entries are actually populated this frame, across every ring -
    // dynamic, since a chunk that's still generating simply isn't in the drawn set yet.
    inline u32 CurrentlyActiveChunks = 0;
    inline std::array<ChunkInstanceData, TotalMaxDrawnChunks> ChunkDrawList;

    // One entry per GPU-resident heightmap slot within one ring.
    struct CacheSlot {
        ivec2 Position = { 0, 0 };
        bool Valid = false;       // has this slot ever actually been generated into
        u64 LastUsedTick = 0;     // last RefreshChunks() call this slot was part of the drawn set
    };

    // Rolling log of the most recent cache evictions, so thrashing (the same positions being
    // repeatedly evicted and regenerated) is visible rather than having to be inferred.
    constexpr u32 EvictionLogSize = 32;

    struct EvictionRecord {
        ivec2 EvictedPosition;
        ivec2 ReplacedByPosition;
        u32 Slot;
        u64 Tick;
    };

    struct Stats {
        u64 ChunksGenerated = 0;      // completed generations since startup (excluding Init's batch)
        u64 Evictions = 0;            // cache slots reclaimed from a valid chunk
        u64 GenerationsStarted = 0;
        f32 LastGenerationMs = 0;     // wall time of the most recently finalized generation
        u32 GenerationsInFlight = 0;  // how many of this ring's GenerationPool slots are busy right now
        u32 DrawnLastFrame = 0;       // after frustum culling
        u32 CulledLastFrame = 0;      // cache-resident but rejected by the frustum test
        u32 VisibleMissingLastFrame = 0;  // in frustum but not yet generated - the real "dark chunk" count
    };

    // One in-flight async heightmap generation - one slot of a ring's GenerationPool.
    struct PendingGeneration {
        Heightmap StagingData;             // worker writes here, never into the shared HeightmapData
        PropPlacement StagingProps;        // worker writes here too - same lifetime as StagingData,
                                            // finalized into ring.Props at the same point (props
                                            // piggyback on the heightmap's generation/cache
                                            // lifecycle, no second cache/LRU state machine)
        ivec2 Position = { 0, 0 };
        u32 TargetLayer = 0;
        f64 WorldStep = 1.0;               // this ring's world-space-per-texel step
        std::chrono::steady_clock::time_point StartTime;
        std::atomic<bool> Done{false};     // release by worker, acquire by main thread
        bool InFlight = false;             // main-thread-only, no atomics needed
    };

    // How many chunks a single ring can generate concurrently - a player moving fast enough brings
    // multiple positions into range within the same few frames, so a single in-flight generation
    // per ring left the rest sitting as visible holes until each prior one finished.
    constexpr u32 GenerationPoolSize = 8;

    // One ring's complete streaming/cache state. Templated on its own
    // MaxDrawnChunks/MaxCachedChunks so every ring stays exactly as tightly, statically sized as
    // ring 0 always was - no heap-allocated or worst-case-shared arrays.
    template<u32 MaxDrawnChunksV, u32 MaxCachedChunksV>
    struct RingState {
        static constexpr u32 DrawnCapacity = MaxDrawnChunksV;
        static constexpr u32 CachedCapacity = MaxCachedChunksV;

        f64 ChunkScale = 0.0;
        // How far this ring's own candidate loop scans (chunk-grid units, this ring's own scale).
        // Ring 0's InnerRadius is 0, so its ScanRadius is an ordinary from-the-player radius; every
        // outer ring's ScanRadius reaches all the way to its own OuterRadius, with everything
        // closer than InnerRadius skipped by the candidate loop itself.
        u32 ScanRadius = 0;
        f64 InnerRadius = 0.0;       // world-space distance where this ring starts owning chunks
        f64 OuterRadius = 0.0;       // world-space distance where the next ring out takes over
        u32 LayerOffset = 0;         // this ring's starting index into the shared heightmap texture array

        std::array<CacheSlot, MaxCachedChunksV> Cache;
        std::array<Heightmap, MaxCachedChunksV> HeightmapData;
        std::array<PropPlacement, MaxCachedChunksV> Props;  // parallel to HeightmapData, same index
        std::unordered_map<u64, u32> PositionToSlot;
        std::array<PendingGeneration, GenerationPoolSize> GenerationPool;

        std::array<EvictionRecord, EvictionLogSize> EvictionLog{};
        u32 EvictionLogCount = 0;
        u32 EvictionLogCursor = 0;

        Stats DebugStats;
    };

    using Ring0State = RingState<MaxDrawnChunks, MaxCachedChunks>;
    using OuterRingState = RingState<OuterRingMaxDrawnChunks, OuterRingMaxCachedChunks>;

    inline Ring0State Ring0;
    inline std::array<OuterRingState, OuterRingCount> OuterRings;

    // Given a chunk's global heightmap-array layer (ChunkInstanceData::TextureLayer), returns that
    // chunk's prop placement data. Layer offsets are ring 0's fixed layer count followed by
    // equally-sized outer-ring blocks (see Init()'s layer_cursor), so which ring owns a layer - and
    // its local slot - is closed-form arithmetic, not a search. Lets PropPass go straight from a
    // drawn chunk's TextureLayer to its props without knowing the ring array split exists.
    inline const PropPlacement& GetPropsForLayer(u32 global_layer) {
        if (global_layer < MaxCachedChunks) {
            return Ring0.Props[global_layer];
        }
        u32 offset = global_layer - MaxCachedChunks;
        u32 ring_index = offset / OuterRingMaxCachedChunks;
        u32 local_slot = offset % OuterRingMaxCachedChunks;
        return OuterRings[ring_index].Props[local_slot];
    }

    // Debug/validation toggle - lets frustum culling be disabled at runtime to sanity-check its
    // effect (e.g. via ImGui) without a rebuild.
    inline bool CullingEnabled = true;

    // Monotonic per-RefreshChunks-call counter, shared across every ring, stamped onto a cache
    // slot whenever it's part of the current frame's drawn set - what makes each ring's LRU
    // eviction a "smallest tick wins" scan within that ring's own Cache. Exposed here (used to be
    // TerrainManager.cpp-local) so TerrainDebugTools can compare a chunk's LastUsedTick against
    // "now" without needing its own copy.
    inline u64 CurrentTick = 0;

    // Called by the terrain pass once
    void Init();

    // Called every time the current_player_chunk changes
    void RefreshChunks(vec3 player_position, const Frustum& camera_frustum);

    // --- Diagnostic-only API for TerrainDebugTools - not used by the core streaming loop itself,
    // deliberately narrow and read-only. ---

    // Which ring (0 = Ring0, 1..OuterRingCount = OuterRings[ring_index - 1]) owns a given
    // horizontal (XZ-plane) distance from the player, or -1 if beyond all streamed coverage.
    // Writes that ring's ChunkScale to out_chunk_scale when it returns >= 0.
    i32 FindRingForDistance(f64 horizontal_distance, f64& out_chunk_scale);

    // Full state snapshot for one chunk grid position within one ring (same ring-index convention
    // as FindRingForDistance) - read-only, no effect on streaming.
    struct ChunkDiagnostic {
        bool InCacheMap = false;      // PositionToSlot has an entry for this position
        bool CacheValid = false;      // ...and that slot's CacheSlot::Valid is true
        u64 LastUsedTick = 0;         // CacheSlot::LastUsedTick, only meaningful if CacheValid
        bool InGenerationPool = false; // an in-flight PendingGeneration targets this position
        bool GenerationDone = false;   // ...and its worker thread has finished (pending finalize)
        u32 GlobalLayer = 0;          // ring.LayerOffset + slot, only meaningful if InCacheMap
        bool InDrawListThisFrame = false; // that GlobalLayer actually appears in ChunkDrawList now
        bool IntersectsFrustum = false;   // ChunkBounds(position) against the given frustum, live
    };
    ChunkDiagnostic DiagnoseChunk(i32 ring_index, ivec2 chunk_pos, const Frustum& camera_frustum);
}
