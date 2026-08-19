#include "TerrainManager.hpp"

#include <vector>
#include <cstring>
#include <unordered_map>

#include <FastNoiseLite.hpp>

#include "Renderer/Passes/TerrainPass.hpp"
#include "Renderer/Resources/TransferPipe.hpp"
#include "Engine/Core/TaskScheduler/TaskScheduler.hpp"

namespace TerrainManager {
    void WriteHeightmap(Heightmap& out, ivec2 position);

    FastNoiseLite ContinentalNoise;
    FastNoiseLite MountainNoise;
    FastNoiseLite DetailNoise;

    // Cache position lookup: Phase 1 of RefreshChunks() needs "is this grid position resident,
    // and if so in which slot" for every candidate in the exploration circle, every frame - a
    // linear scan of Cache for each candidate would be O(MaxDrawnChunks * MaxCachedChunks) per
    // frame. Keeping this map in lockstep with Cache (updated at the same two points Cache's
    // Position/Valid change: finalize and eviction-kickoff) makes each lookup O(1) instead.
    u64 PackPosition(ivec2 position) {
        return (static_cast<u64>(static_cast<u32>(position.x)) << 32) | static_cast<u32>(position.y);
    }
    std::unordered_map<u64, u32> PositionToSlot;

    // --- Init(): parallel one-shot batch, blocking until all of it is done ---

    struct InitGenTask {
        ivec2 Position;
        u32 TargetLayer;
        std::atomic<u32>* Counter;
    };

    void InitGenerateHeightmapTask(void* payload, TaskScheduler::WorkerContext&) {
        InitGenTask* task = static_cast<InitGenTask*>(payload);
        // Safe to write straight into the shared array: distinct slot per task, and nothing
        // else reads HeightmapData until Init() returns (render loop hasn't started yet).
        WriteHeightmap(HeightmapData[task->TargetLayer], task->Position);
        task->Counter->fetch_sub(1, std::memory_order_release);
    }

    void Init() {
        ContinentalNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ContinentalNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        ContinentalNoise.SetFractalOctaves(5);
        ContinentalNoise.SetFrequency(0.002f);

        MountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        MountainNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        MountainNoise.SetFractalOctaves(6);
        MountainNoise.SetFrequency(0.015f);

        DetailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        DetailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        DetailNoise.SetFractalOctaves(4);
        DetailNoise.SetFrequency(0.08f);

        // Kickstart the valid data
        vec3 player_pos = {0, 0, 0};
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_pos.x/ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_pos.z/ChunkScale));

        u32 coords_counter = 0;
        i32 radius = ExplorationRadius;
        i32 r_squared = radius*radius;

        // Payloads must outlive their tasks - reserve() up front so push_back() never
        // reallocates (MaxDrawnChunks is this loop's own exact upper bound).
        std::vector<InitGenTask> tasks;
        tasks.reserve(MaxDrawnChunks);
        std::atomic<u32> counter{0};

        // Circle around the player
        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {

                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;

                // Valid point
                if ((dx * dx) + (dy * dy) <= r_squared) {
                    ChunkDrawList[coords_counter] = {
                        .WorldPos = { x, y },
                        .TextureLayer = coords_counter,
                        .padding = 0
                    };
                    Cache[coords_counter] = {
                        .Position = { x, y },
                        .Valid = true,
                        .LastUsedTick = 0
                    };
                    PositionToSlot[PackPosition({ x, y })] = coords_counter;

                    counter.fetch_add(1, std::memory_order_relaxed);
                    tasks.push_back({ .Position = { x, y }, .TargetLayer = coords_counter, .Counter = &counter });
                    TaskScheduler::SubmitTask(InitGenerateHeightmapTask, &tasks.back());

                    coords_counter++;
                }
            }
        }

        CurrentlyActiveChunks = coords_counter;

        TaskScheduler::Wait(counter);

        for (u32 i = 0; i < coords_counter; i++) {
            TransferPipe::QueueImageSliceUpload(
                TerrainPass::Heightmap::Image,
                i,
                &HeightmapData[i],
                sizeof(Heightmap)
            );
        }

        // The heightmap array is sampled through a single whole-array descriptor, so Vulkan
        // requires every layer to already be VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL at draw
        // time - not just the ones actually referenced this frame. Cache slots beyond the
        // initial drawn set have no real data yet (Cache[i].Valid stays false, so they're never
        // picked to draw), but their layer still needs an initial transition; content is
        // irrelevant since nothing samples it until it's regenerated for real.
        Heightmap blank_heightmap{};
        for (u32 i = coords_counter; i < MaxCachedChunks; i++) {
            TransferPipe::QueueImageSliceUpload(
                TerrainPass::Heightmap::Image,
                i,
                &blank_heightmap,
                sizeof(Heightmap)
            );
        }

        TransferPipe::LazySubmit();
    }

    // --- RefreshChunks(): one in-flight generation at a time, polled, never blocking ---

    struct PendingGeneration {
        Heightmap StagingData;            // worker writes here, never into the shared HeightmapData
        ivec2 Position = { 0, 0 };
        u32 TargetLayer = 0;
        std::atomic<bool> Done{false};    // release by worker, acquire by main thread
        bool InFlight = false;            // main-thread-only, no atomics needed
    };
    PendingGeneration Generation;

    void GenerateHeightmapTask(void* payload, TaskScheduler::WorkerContext&) {
        PendingGeneration* generation = static_cast<PendingGeneration*>(payload);
        WriteHeightmap(generation->StagingData, generation->Position);
        generation->Done.store(true, std::memory_order_release);
    }

    // Monotonic per-RefreshChunks-call counter, stamped onto a cache slot whenever it's part of
    // the current frame's drawn set - what makes LRU eviction a "smallest tick wins" scan.
    u64 CurrentTick = 0;

    // Picks a cache slot to generate a new chunk into: prefer one that's never been used, else
    // evict the least-recently-used slot - which is never one of this frame's drawn slots, since
    // RefreshChunks() already stamped all of those to CurrentTick before this is ever called.
    u32 PickSlotToGenerateInto() {
        for (u32 i = 0; i < MaxCachedChunks; i++) {
            if (!Cache[i].Valid) { return i; }
        }

        u32 oldest = 0;
        u64 oldest_tick = UINT64_MAX;
        for (u32 i = 0; i < MaxCachedChunks; i++) {
            if (Cache[i].LastUsedTick < oldest_tick) {
                oldest_tick = Cache[i].LastUsedTick;
                oldest = i;
            }
        }
        return oldest;
    }

    void RefreshChunks(vec3 player_position) {
        CurrentTick++;

        // Phase 1: rebuild the drawn list from the cache as it stood at the start of this call -
        // deliberately BEFORE finalizing any generation that just completed (see phase 2 below),
        // so a chunk never gets added to the drawn list in the same frame its GPU upload was
        // queued. That upload's release/write/acquire chain hasn't had a chance to actually run
        // yet at this point - sampling it this frame would race ahead of it. Waiting until next
        // frame gives AcquirePending (called every frame from Renderer::Frame()) a full cycle to
        // actually acquire it and for the draw's own wait-on-last-acquire to cover it correctly.
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_position.x / ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_position.z / ChunkScale));

        constexpr i32 radius = ExplorationRadius;
        constexpr i32 r_squared = radius * radius;

        CurrentlyActiveChunks = 0;
        ivec2 missing_position = { 0, 0 };
        bool found_missing = false;

        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {
                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;
                if ((dx * dx) + (dy * dy) > r_squared) { continue; }

                ivec2 candidate = { x, y };

                u32 cache_index = UINT32_MAX;
                auto slot_it = PositionToSlot.find(PackPosition(candidate));
                if (slot_it != PositionToSlot.end()) {
                    cache_index = slot_it->second;
                }

                if (cache_index != UINT32_MAX) {
                    // Cache hit - draw it, mark it recently used
                    Cache[cache_index].LastUsedTick = CurrentTick;
                    ChunkDrawList[CurrentlyActiveChunks] = {
                        .WorldPos = candidate,
                        .TextureLayer = cache_index,
                        .padding = 0
                    };
                    CurrentlyActiveChunks++;
                } else if (!found_missing) {
                    // Only ever kick off one generation per call - remember the first miss
                    missing_position = candidate;
                    found_missing = true;
                }
            }
        }

        // Phase 2: finalize a generation that finished since last frame, landing it in the cache -
        // deliberately AFTER this call's drawn-list rebuild above, so it only becomes eligible to
        // be drawn starting next frame (see the comment on phase 1 for why).
        if (Generation.InFlight && Generation.Done.load(std::memory_order_acquire)) {
            std::memcpy(HeightmapData[Generation.TargetLayer], Generation.StagingData, sizeof(Heightmap));

            Cache[Generation.TargetLayer] = {
                .Position = Generation.Position,
                .Valid = true,
                .LastUsedTick = CurrentTick
            };
            PositionToSlot[PackPosition(Generation.Position)] = Generation.TargetLayer;

            TransferPipe::QueueImageSliceUpload(
                TerrainPass::Heightmap::Image,
                Generation.TargetLayer,
                &HeightmapData[Generation.TargetLayer],
                sizeof(Heightmap)
            );
            TransferPipe::SubmitReleaseAndWrite();

            // missing_position was computed before this finalization - if it's the position we
            // just resolved, it's not actually missing anymore, so don't re-trigger for it.
            if (found_missing && Generation.Position == missing_position) {
                found_missing = false;
            }

            Generation.InFlight = false;
        }

        // Phase 3: kick off generation for the first missing position, if nothing is already in flight
        if (found_missing && !Generation.InFlight) {
            Generation.Position = missing_position;
            Generation.TargetLayer = PickSlotToGenerateInto();

            // Invalidate the slot the instant it's claimed for reuse, not only once generation
            // finishes. Otherwise, for however many frames generation takes, this slot is still
            // a "valid" cache hit for whatever position it used to hold - if that old position
            // is still in range, it keeps getting drawn with data that's about to be silently
            // replaced by a completely unrelated position's terrain the moment finalize lands,
            // instead of cleanly showing as missing and getting regenerated on its own.
            if (Cache[Generation.TargetLayer].Valid) {
                PositionToSlot.erase(PackPosition(Cache[Generation.TargetLayer].Position));
            }
            Cache[Generation.TargetLayer].Valid = false;

            Generation.Done.store(false, std::memory_order_relaxed);
            Generation.InFlight = true;

            TaskScheduler::SubmitTask(GenerateHeightmapTask, &Generation);
        }
    }

    void WriteHeightmap(Heightmap& out, ivec2 position) {
        i32 terrain_res = VerticesPerEdge;

        f32 global_x, global_z;
        for (i32 x = 0; x < terrain_res; x++) {
            global_x = static_cast<f32>(x + ((terrain_res-1) * position.x));
            for (i32 z = 0; z < terrain_res; z++) {
                global_z = static_cast<f32>(z + ((terrain_res-1) * position.y));

                f32 cont = (ContinentalNoise.GetNoise(global_x, global_z) + 1.0f) * 0.5f;
                f32 mount = (MountainNoise.GetNoise(global_x, global_z) + 1.0f) * 0.5f;
                f32 detail = (DetailNoise.GetNoise(global_x, global_z) + 1.0f) * 0.5f;

                cont = std::pow(cont, 1.5f);

                f32 mountain_mask = 0.0f;
                if (cont > 0.45f) {
                    mountain_mask = (cont - 0.45f) / 0.55f;
                    mountain_mask = mountain_mask * mountain_mask * (3.0f - 2.0f * mountain_mask);
                }

                f32 detail_mask = .05f;
                if (mountain_mask <= 0.5f) {
                    detail_mask = 3.0f * ((.05f - mountain_mask)*.05f);
                }

                f32 elevation = (cont * 0.30f) + (mount * mountain_mask * 0.65f) + (detail * detail_mask);

                if (elevation <= 0.0) {
                    elevation = 0.0;
                } else if (elevation >= 1.0) {
                    elevation = 1.0;
                }

                f32 remapped = elevation * 65535.0f;
                u16 end_value = static_cast<u16>(remapped);
                out[x][z] = end_value;
            }
        }
    }
}
