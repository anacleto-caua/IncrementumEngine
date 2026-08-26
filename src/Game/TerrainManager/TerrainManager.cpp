#include "TerrainManager.hpp"

#include <chrono>
#include <vector>
#include <cstring>
#include <random>
#include <algorithm>
#include <unordered_map>

#include <FastNoiseLite.hpp>

#include "Game/Game.hpp"
#include "Engine/Core/TaskScheduler/TaskScheduler.hpp"

namespace TerrainManager {
    void WriteHeightmap(Heightmap& out, ivec2 position, f64 world_step);
    void GeneratePropPlacements(PropPlacement& out, const Heightmap& heightmap, ivec2 position, f64 chunk_scale);

    FastNoiseLite ContinentalNoise;
    FastNoiseLite MountainNoise;
    FastNoiseLite DetailNoise;

    // Cache position lookup: Phase 1 of RefreshRing() needs "is this grid position resident, and
    // if so in which slot" for every candidate in a ring's exploration circle, every frame - a
    // linear scan of a ring's Cache for each candidate would be O(drawn * cached) per ring per
    // frame. Keeping each ring's own map in lockstep with its Cache (updated at the same two
    // points Cache's Position/Valid change: finalize and eviction-kickoff) makes each lookup O(1)
    // instead.
    u64 PackPosition(ivec2 position) {
        return (static_cast<u64>(static_cast<u32>(position.x)) << 32) | static_cast<u32>(position.y);
    }

    // World-space bounds of a chunk, for frustum culling. Y uses the full [0, HeightScale] range
    // the shader declares rather than this chunk's actual min/max height - conservative (never
    // wrongly culls a chunk) at the cost of not culling chunks whose real geometry doesn't reach
    // the top of that range. `chunk_scale` is the owning ring's ChunkScale.
    AABB ChunkBounds(ivec2 world_pos, f64 chunk_scale) {
        f32 min_x = static_cast<f32>(world_pos.x) * static_cast<f32>(chunk_scale);
        f32 min_z = static_cast<f32>(world_pos.y) * static_cast<f32>(chunk_scale);
        return {
            .Min = { min_x, 0.0f, min_z },
            .Max = { min_x + static_cast<f32>(chunk_scale), GTerrainPass.Config.HeightScale, min_z + static_cast<f32>(chunk_scale) }
        };
    }

    // --- Init(): ring 0's initial batch is parallel and blocking; outer rings start empty and
    // stream in through RefreshChunks() like anything else ---

    struct InitGenTask {
        ivec2 Position;
        u32 TargetLayer;
        f64 WorldStep;
        std::atomic<u32>* Counter;
    };

    void InitGenerateHeightmapTask(void* payload, TaskScheduler::WorkerContext&) {
        InitGenTask* task = static_cast<InitGenTask*>(payload);
        // Safe to write straight into the shared array: distinct slot per task, and nothing
        // else reads Ring0.HeightmapData until Init() returns (render loop hasn't started yet).
        WriteHeightmap(Ring0.HeightmapData[task->TargetLayer], task->Position, task->WorldStep);
        // Same safety argument covers Ring0.Props - placements are derived from the heightmap
        // just written, same slot, same "nothing reads it until Init() returns" window.
        GeneratePropPlacements(
            Ring0.Props[task->TargetLayer],
            Ring0.HeightmapData[task->TargetLayer],
            task->Position,
            task->WorldStep * static_cast<f64>(VerticesPerEdge - 1)
        );
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

        // --- Ring geometry: fixed for the whole run, computed once here ---
        Ring0.ChunkScale = ChunkScale;
        Ring0.ScanRadius = ExplorationRadius;
        Ring0.InnerRadius = 0.0;
        Ring0.OuterRadius = ChunkScale * static_cast<f64>(ExplorationRadius);
        Ring0.LayerOffset = 0;

        f64 previous_outer_radius = Ring0.OuterRadius;
        u32 layer_cursor = MaxCachedChunks;
        for (u32 i = 0; i < OuterRingCount; i++) {
            OuterRings[i].ChunkScale = OuterRingChunkScales[i];
            // Reaches all the way to this ring's own OuterRadius, not just the annulus width -
            // see the comment on RingState::ScanRadius and on OuterRingScanRadius in the header.
            OuterRings[i].ScanRadius = OuterRingScanRadius;
            OuterRings[i].InnerRadius = previous_outer_radius;
            OuterRings[i].OuterRadius = previous_outer_radius + OuterRings[i].ChunkScale * static_cast<f64>(OuterRingExplorationRadius);
            OuterRings[i].LayerOffset = layer_cursor;

            previous_outer_radius = OuterRings[i].OuterRadius;
            layer_cursor += OuterRingMaxCachedChunks;
        }

        // Kickstart the valid data - ring 0 only, exactly as before this plan.
        vec3 player_pos = {0, 0, 0};
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_pos.x/ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_pos.z/ChunkScale));

        u32 coords_counter = 0;
        i32 radius = ExplorationRadius;
        i32 r_squared = radius*radius;
        f64 world_step = ChunkScale / static_cast<f64>(VerticesPerEdge - 1);

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
                        .TextureLayer = coords_counter,   // Ring0.LayerOffset == 0
                        .Scale = static_cast<f32>(ChunkScale)
                    };
                    Ring0.Cache[coords_counter] = {
                        .Position = { x, y },
                        .Valid = true,
                        .LastUsedTick = 0
                    };
                    Ring0.PositionToSlot[PackPosition({ x, y })] = coords_counter;

                    counter.fetch_add(1, std::memory_order_relaxed);
                    tasks.push_back({ .Position = { x, y }, .TargetLayer = coords_counter, .WorldStep = world_step, .Counter = &counter });
                    TaskScheduler::SubmitTask(InitGenerateHeightmapTask, &tasks.back());

                    coords_counter++;
                }
            }
        }

        CurrentlyActiveChunks = coords_counter;

        TaskScheduler::Wait(counter);

        for (u32 i = 0; i < coords_counter; i++) {
            GTerrainPass.Heightmap.QueueSlice(i, &Ring0.HeightmapData[i], sizeof(Heightmap));
        }

        // The heightmap array is sampled through a single whole-array descriptor, so Vulkan
        // requires every layer to already be VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL at draw
        // time - not just the ones actually referenced this frame. That covers ring 0's own
        // unfilled cache slots and every outer ring's entirely-empty cache (outer rings fill in
        // for real over the following frames via RefreshChunks()); content is irrelevant since
        // nothing samples it until it's regenerated for real.
        Heightmap blank_heightmap{};
        for (u32 i = coords_counter; i < TotalMaxCachedChunks; i++) {
            GTerrainPass.Heightmap.QueueSlice(i, &blank_heightmap, sizeof(Heightmap));
        }

        GTransferPipe.LazySubmit();
    }

    // --- RefreshChunks(): one in-flight generation at a time PER RING, polled, never blocking ---

    void GenerateHeightmapTask(void* payload, TaskScheduler::WorkerContext&) {
        PendingGeneration* generation = static_cast<PendingGeneration*>(payload);
        WriteHeightmap(generation->StagingData, generation->Position, generation->WorldStep);
        GeneratePropPlacements(
            generation->StagingProps,
            generation->StagingData,
            generation->Position,
            generation->WorldStep * static_cast<f64>(VerticesPerEdge - 1)
        );
        generation->Done.store(true, std::memory_order_release);
    }

    // Picks a cache slot to generate a new chunk into, within one ring: prefer one that's never
    // been used, else evict the least-recently-used slot - which is never one of this frame's
    // drawn slots, since RefreshRing() already stamped all of those to CurrentTick before this is
    // ever called. Also skips any slot already claimed by another in-flight generation in this
    // same ring's pool - without this, two generations kicked off in the same RefreshRing() call
    // could both target the same cache slot (invalidating it doesn't remove it from consideration
    // on its own; only tracking "is some pool entry already writing here" does).
    template<typename RingT>
    u32 PickSlotToGenerateInto(RingT& ring) {
        u32 count = static_cast<u32>(ring.Cache.size());

        auto is_claimed = [&](u32 slot) {
            for (const PendingGeneration& gen : ring.GenerationPool) {
                if (gen.InFlight && gen.TargetLayer == slot) { return true; }
            }
            return false;
        };

        for (u32 i = 0; i < count; i++) {
            if (!ring.Cache[i].Valid && !is_claimed(i)) { return i; }
        }

        u32 oldest = 0;
        u64 oldest_tick = UINT64_MAX;
        for (u32 i = 0; i < count; i++) {
            if (is_claimed(i)) { continue; }
            if (ring.Cache[i].LastUsedTick < oldest_tick) {
                oldest_tick = ring.Cache[i].LastUsedTick;
                oldest = i;
            }
        }
        return oldest;
    }

    // Three-phase per-ring refresh: rebuild draw list / finalize a finished generation / kick off
    // the next missing ones. Shared by ring 0 and every outer ring via `ring`'s own state and
    // LODRing config (baked in by Init()). Returns true if this ring queued any completed chunk's
    // slice this call - the caller batches this across every ring into one SubmitReleaseAndWrite()
    // call per frame; a per-ring call point hit a real GPU-timing assertion failure once the pool
    // let several rings each queue several chunks in close succession.
    template<typename RingT>
    bool RefreshRing(RingT& ring, vec3 player_position, const Frustum& camera_frustum, u64 current_tick, u32& draw_cursor) {
        // Phase 1: rebuild this ring's slice of the shared draw list from its cache as it stood
        // at the start of this call - deliberately BEFORE finalizing any generation that just
        // completed (see phase 2 below), so a chunk never gets added to the drawn list in the
        // same frame its GPU upload was queued. That upload's release/write/acquire chain hasn't
        // had a chance to actually run yet at this point - sampling it this frame would race
        // ahead of it. Waiting until next frame gives AcquirePending (called every frame from
        // Renderer::Frame()) a full cycle to actually acquire it and for the draw's own
        // wait-on-last-acquire to cover it correctly.
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_position.x / ring.ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_position.z / ring.ChunkScale));

        i32 radius = static_cast<i32>(ring.ScanRadius);
        i32 r_squared = radius * radius;

        // The ring's own PoolCapacity best missing candidates found this pass, kept sorted
        // ascending by priority (index 0 = best) as the scan runs - NOT the first PoolCapacity
        // found in scan order. Visible (per the same frustum test used for cached chunks below)
        // beats not-visible outright; ties within the same visibility go to the closer one - taking
        // scan-order candidates instead meant a region late in the raster scan could sit
        // unresolved for many frames even while directly in view.
        struct PrioritizedMiss {
            ivec2 Position;
            bool Visible;
            i64 DistSq;   // chunk-grid units, this ring's own scale - only compared within this ring
        };
        auto is_better = [](const PrioritizedMiss& a, const PrioritizedMiss& b) {
            if (a.Visible != b.Visible) { return a.Visible; }
            return a.DistSq < b.DistSq;
        };
        std::array<PrioritizedMiss, RingT::PoolCapacity> best_missing;
        u32 best_count = 0;
        u32 culled_count = 0;
        u32 drawn_count = 0;
        u32 visible_missing_count = 0;

        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {
                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;
                if ((dx * dx) + (dy * dy) > r_squared) { continue; }

                // Ring ownership is exclusive, not overlapping: skip anything already owned by a
                // strictly-inner ring. Ring 0 has InnerRadius == 0, so this never skips anything
                // for it. `world_x`/`world_z` are this candidate's true, absolute near-corner world
                // position, compared against a smooth InnerRadius circle centered on the player's
                // actual continuous position - NOT `dx`/`dy * ring.ChunkScale`, which would measure
                // distance from the CORNER of this ring's own floor()'d player_coord chunk instead
                // of from the player itself, silently adding up to one full ring.ChunkScale of
                // error per axis depending on where the player sits within that chunk (a real,
                // previously-unaccounted error source on top of the one below). The inner ring's
                // actual coverage is also a lattice disc at its own (finer) scale and its own
                // player_coord, which disagrees with a smooth circle worst along the diagonals -
                // a candidate both rings believe the other owns ends up drawn by neither. Shrinking
                // the exclusion radius by one chunk of this ring's own scale trades that gap for a
                // thin band of double coverage near the seam instead.
                if (ring.InnerRadius > 0.0) {
                    f64 safe_inner_radius = ring.InnerRadius - ring.ChunkScale;
                    if (safe_inner_radius > 0.0) {
                        f64 world_x = static_cast<f64>(x) * ring.ChunkScale;
                        f64 world_z = static_cast<f64>(y) * ring.ChunkScale;
                        f64 true_dx = world_x - static_cast<f64>(player_position.x);
                        f64 true_dz = world_z - static_cast<f64>(player_position.z);
                        if ((true_dx * true_dx + true_dz * true_dz) < safe_inner_radius * safe_inner_radius) {
                            continue;
                        }
                    }
                }

                ivec2 candidate = { x, y };

                u32 cache_index = UINT32_MAX;
                auto slot_it = ring.PositionToSlot.find(PackPosition(candidate));
                if (slot_it != ring.PositionToSlot.end()) {
                    cache_index = slot_it->second;
                }

                if (cache_index != UINT32_MAX) {
                    // Cache hit - always mark recently used regardless of visibility, so turning
                    // away from a chunk doesn't make it LRU-evict while it's still within this
                    // ring's ExplorationRadius.
                    ring.Cache[cache_index].LastUsedTick = current_tick;
                    bool intersects = Intersects(camera_frustum, ChunkBounds(candidate, ring.ChunkScale));

                    if (
                        (!CullingEnabled || intersects) &&
                        (draw_cursor < TotalMaxDrawnChunks)
                        ) {
                        ChunkDrawList[draw_cursor] = {
                            .WorldPos = candidate,
                            .TextureLayer = cache_index + ring.LayerOffset,
                            .Scale = static_cast<f32>(ring.ChunkScale)
                        };
                        CurrentlyActiveChunks++;
                        draw_cursor++;
                        drawn_count++;
                    } else {
                        culled_count++;
                    }
                } else {
                    // Visible-but-missing counts regardless of already-in-flight status below -
                    // an in-flight chunk is still visibly dark on screen until it actually finishes,
                    // so it belongs in this "how many dark chunks are in view right now" stat too.
                    bool visible = !CullingEnabled || Intersects(camera_frustum, ChunkBounds(candidate, ring.ChunkScale));
                    if (visible) { visible_missing_count++; }

                    bool already_in_flight = false;
                    for (const PendingGeneration& gen : ring.GenerationPool) {
                        if (gen.InFlight && gen.Position == candidate) {
                            already_in_flight = true;
                            break;
                        }
                    }

                    if (!already_in_flight) {
                        PrioritizedMiss candidate_info = {
                            .Position = candidate,
                            .Visible = visible,
                            .DistSq = static_cast<i64>(dx) * dx + static_cast<i64>(dy) * dy
                        };

                        // Insertion into the small (K=RingT::PoolCapacity) sorted buffer - cheaper
                        // and more legible than pulling in <algorithm> for a size this small.
                        if (best_count < RingT::PoolCapacity) {
                            u32 insert_at = best_count;
                            while (insert_at > 0 && is_better(candidate_info, best_missing[insert_at - 1])) {
                                best_missing[insert_at] = best_missing[insert_at - 1];
                                insert_at--;
                            }
                            best_missing[insert_at] = candidate_info;
                            best_count++;
                        } else if (is_better(candidate_info, best_missing[RingT::PoolCapacity - 1])) {
                            u32 insert_at = RingT::PoolCapacity - 1;
                            while (insert_at > 0 && is_better(candidate_info, best_missing[insert_at - 1])) {
                                best_missing[insert_at] = best_missing[insert_at - 1];
                                insert_at--;
                            }
                            best_missing[insert_at] = candidate_info;
                        }
                    }
                }
            }
        }

        // Phase 2: finalize a generation that finished since last frame, landing it in the cache -
        // deliberately AFTER this call's drawn-list rebuild above, so it only becomes eligible to
        // be drawn starting next frame (see the comment on phase 1 for why).
        ring.DebugStats.DrawnLastFrame = drawn_count;
        ring.DebugStats.CulledLastFrame = culled_count;
        ring.DebugStats.VisibleMissingLastFrame = visible_missing_count;

        // Queue every completed generation's slice - the caller submits once for the whole
        // frame's batch across every ring (see the comment on this function).
        bool any_finalized = false;
        for (PendingGeneration& gen : ring.GenerationPool) {
            if (!gen.InFlight || !gen.Done.load(std::memory_order_acquire)) { continue; }

            std::memcpy(ring.HeightmapData[gen.TargetLayer], gen.StagingData, sizeof(Heightmap));
            ring.Props[gen.TargetLayer] = gen.StagingProps;

            ring.DebugStats.ChunksGenerated++;
            ring.DebugStats.LastGenerationMs =
                std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - gen.StartTime).count();

            ring.Cache[gen.TargetLayer] = {
                .Position = gen.Position,
                .Valid = true,
                .LastUsedTick = current_tick
            };
            ring.PositionToSlot[PackPosition(gen.Position)] = gen.TargetLayer;

            GTerrainPass.Heightmap.QueueSlice(
                gen.TargetLayer + ring.LayerOffset,
                &ring.HeightmapData[gen.TargetLayer],
                sizeof(Heightmap)
            );
            any_finalized = true;

            gen.InFlight = false;
        }

        // Phase 3: kick off generation for the best-scoring missing positions found this pass, up
        // to GenerationPoolSize concurrently instead of strictly one at a time.
        for (u32 m = 0; m < best_count; m++) {
            PendingGeneration* free_slot = nullptr;
            for (PendingGeneration& gen : ring.GenerationPool) {
                if (!gen.InFlight) { free_slot = &gen; break; }
            }
            if (!free_slot) { break; } // every pool slot busy - the rest wait for next frame

            PendingGeneration& gen = *free_slot;
            gen.Position = best_missing[m].Position;
            gen.TargetLayer = PickSlotToGenerateInto(ring);
            gen.WorldStep = ring.ChunkScale / static_cast<f64>(VerticesPerEdge - 1);

            // Invalidate the slot the instant it's claimed for reuse, not only once generation
            // finishes. Otherwise, for however many frames generation takes, this slot is still
            // a "valid" cache hit for whatever position it used to hold - if that old position
            // is still in range, it keeps getting drawn with data that's about to be silently
            // replaced by a completely unrelated position's terrain the moment finalize lands,
            // instead of cleanly showing as missing and getting regenerated on its own.
            if (ring.Cache[gen.TargetLayer].Valid) {
                ring.PositionToSlot.erase(PackPosition(ring.Cache[gen.TargetLayer].Position));

                ring.EvictionLog[ring.EvictionLogCursor] = {
                    .EvictedPosition = ring.Cache[gen.TargetLayer].Position,
                    .ReplacedByPosition = gen.Position,
                    .Slot = gen.TargetLayer,
                    .Tick = current_tick
                };
                ring.EvictionLogCursor = (ring.EvictionLogCursor + 1) % EvictionLogSize;
                if (ring.EvictionLogCount < EvictionLogSize) { ring.EvictionLogCount++; }
                ring.DebugStats.Evictions++;
            }
            ring.Cache[gen.TargetLayer].Valid = false;

            gen.Done.store(false, std::memory_order_relaxed);
            gen.InFlight = true;
            gen.StartTime = std::chrono::steady_clock::now();
            ring.DebugStats.GenerationsStarted++;

            // The pool-slot reservation above prioritizes (Visible, DistSq) for *which* candidate
            // gets a slot, but says nothing about the order TaskScheduler's worker threads actually
            // run submitted work in. TaskPriority::High routes a currently-visible chunk into a
            // separate, always-drained-first queue tier so it can't get stuck behind older,
            // now-less-relevant submissions under a big enough burst.
            TaskScheduler::SubmitTask(
                GenerateHeightmapTask,
                &gen,
                best_missing[m].Visible ? TaskScheduler::TaskPriority::High : TaskScheduler::TaskPriority::Normal
            );
        }

        u32 in_flight_count = 0;
        for (const PendingGeneration& gen : ring.GenerationPool) { if (gen.InFlight) { in_flight_count++; } }
        ring.DebugStats.GenerationsInFlight = in_flight_count;

        return any_finalized;
    }

    void RefreshChunks(vec3 player_position, const Frustum& camera_frustum) {
        CurrentTick++;
        CurrentlyActiveChunks = 0;

        u32 draw_cursor = 0;

        bool any_finalized = RefreshRing(Ring0, player_position, camera_frustum, CurrentTick, draw_cursor);
        for (u32 i = 0; i < OuterRingCount; i++) {
            any_finalized |= RefreshRing(OuterRings[i], player_position, camera_frustum, CurrentTick, draw_cursor);
        }

        if (any_finalized) { GTransferPipe.SubmitReleaseAndWrite(); }
    }

    i32 FindRingForDistance(f64 horizontal_distance, f64& out_chunk_scale) {
        if (horizontal_distance <= Ring0.OuterRadius) {
            out_chunk_scale = Ring0.ChunkScale;
            return 0;
        }
        for (u32 i = 0; i < OuterRingCount; i++) {
            if (horizontal_distance <= OuterRings[i].OuterRadius) {
                out_chunk_scale = OuterRings[i].ChunkScale;
                return static_cast<i32>(i) + 1;
            }
        }
        return -1;
    }

    // Shared by DiagnoseChunk for both ring types (Ring0State and OuterRingState are different
    // template instantiations, but identical in every field this needs to read).
    template<typename RingT>
    ChunkDiagnostic DiagnoseChunkInRing(RingT& ring, u32 ring_index_for_layer, ivec2 chunk_pos, const Frustum& camera_frustum) {
        ChunkDiagnostic diag;

        auto found = ring.PositionToSlot.find(PackPosition(chunk_pos));
        diag.InCacheMap = (found != ring.PositionToSlot.end());
        if (diag.InCacheMap) {
            u32 slot = found->second;
            diag.CacheValid = ring.Cache[slot].Valid;
            diag.LastUsedTick = ring.Cache[slot].LastUsedTick;
            diag.GlobalLayer = ring.LayerOffset + slot;

            if (diag.CacheValid) {
                for (u32 i = 0; i < CurrentlyActiveChunks; i++) {
                    if (ChunkDrawList[i].TextureLayer == diag.GlobalLayer) {
                        diag.InDrawListThisFrame = true;
                        break;
                    }
                }
            }
        }

        for (const PendingGeneration& gen : ring.GenerationPool) {
            if (gen.InFlight && gen.Position == chunk_pos) {
                diag.InGenerationPool = true;
                diag.GenerationDone = gen.Done.load(std::memory_order_relaxed);
                break;
            }
        }

        diag.IntersectsFrustum = Intersects(camera_frustum, ChunkBounds(chunk_pos, ring.ChunkScale));

        (void)ring_index_for_layer; // kept as a parameter for symmetry/future use, not currently needed
        return diag;
    }

    ChunkDiagnostic DiagnoseChunk(i32 ring_index, ivec2 chunk_pos, const Frustum& camera_frustum) {
        if (ring_index == 0) {
            return DiagnoseChunkInRing(Ring0, 0, chunk_pos, camera_frustum);
        }
        if (ring_index >= 1 && static_cast<u32>(ring_index - 1) < OuterRingCount) {
            u32 outer_index = static_cast<u32>(ring_index - 1);
            return DiagnoseChunkInRing(OuterRings[outer_index], static_cast<u32>(ring_index), chunk_pos, camera_frustum);
        }
        return {}; // invalid ring_index - caller error, return an all-false diagnostic rather than asserting
    }

    void WriteHeightmap(Heightmap& out, ivec2 position, f64 world_step) {
        i32 terrain_res = VerticesPerEdge;

        f32 global_x, global_z;
        for (i32 x = 0; x < terrain_res; x++) {
            global_x = static_cast<f32>(static_cast<f64>(x + ((terrain_res-1) * position.x)) * world_step);
            for (i32 z = 0; z < terrain_res; z++) {
                global_z = static_cast<f32>(static_cast<f64>(z + ((terrain_res-1) * position.y)) * world_step);

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

    // Scatters props across one chunk, sampling the heightmap this same generation task just
    // wrote (no GPU round-trip needed - this runs CPU-side, same worker thread, same slot).
    // World position is derived with the *exact* formula terrain.vert uses to place a heightmap
    // texel (texel (tx,tz) -> u=tx/(RES-1), v=tz/(RES-1) -> world.x = v*scale + WorldPos.x*scale,
    // world.z = u*scale + WorldPos.y*scale) rather than re-deriving it independently, so props
    // land exactly on the rendered surface regardless of which axis terrain.vert's u/v happen to
    // be named after.
    void GeneratePropPlacements(PropPlacement& out, const Heightmap& heightmap, ivec2 position, f64 chunk_scale) {
        out.Count = 0;

        // Deterministic per-chunk RNG - no persistence needed, terrain itself is fully procedural
        // and regenerable, so props regenerate identically every time this chunk is (re)streamed.
        u64 seed = (static_cast<u64>(static_cast<u32>(position.x)) << 32)
                 ^ static_cast<u64>(static_cast<u32>(position.y))
                 ^ 0x9E3779B97F4A7C15ULL;
        std::mt19937 rng(static_cast<u32>(seed ^ (seed >> 32)));
        std::uniform_real_distribution<f32> jitter(-0.4f, 0.4f);
        std::uniform_real_distribution<f32> rotation_dist(0.0f, 6.28318530f);
        std::uniform_real_distribution<f32> scale_dist(0.8f, 1.2f);

        constexpr u32 GridDim = 5;  // 5x5 jittered grid = 25 candidates, capped at MaxPropsPerChunk
        constexpr u32 LastTexel = VerticesPerEdge - 1;

        for (u32 gx = 0; gx < GridDim && out.Count < MaxPropsPerChunk; gx++) {
            for (u32 gz = 0; gz < GridDim && out.Count < MaxPropsPerChunk; gz++) {
                f32 cell_u = (static_cast<f32>(gx) + 0.5f + jitter(rng)) / static_cast<f32>(GridDim);
                f32 cell_v = (static_cast<f32>(gz) + 0.5f + jitter(rng)) / static_cast<f32>(GridDim);
                cell_u = std::clamp(cell_u, 0.01f, 0.99f);
                cell_v = std::clamp(cell_v, 0.01f, 0.99f);

                // NOT heightmap[cell_u][cell_v] - the heightmap image is uploaded tightly-packed
                // from Heightmap[x][z] (z fastest-varying), which makes the image's WIDTH axis
                // ("u") address Heightmap's Z index and HEIGHT ("v") address X - the opposite of
                // the naive expectation. terrain.vert's own u/v -> world.x/world.z swap cancels
                // this out; this code has to replicate that same composition to sample the texel
                // terrain.vert would for a given (u=cell_u, v=cell_v): Heightmap[X = v][Z = u].
                u32 tx = static_cast<u32>(cell_v * static_cast<f32>(LastTexel));
                u32 tz = static_cast<u32>(cell_u * static_cast<f32>(LastTexel));

                f32 h_center = static_cast<f32>(heightmap[tx][tz]) / 65535.0f;
                f32 h_dx = static_cast<f32>(heightmap[std::min(tx + 1, LastTexel)][tz]) / 65535.0f;
                f32 h_dz = static_cast<f32>(heightmap[tx][std::min(tz + 1, LastTexel)]) / 65535.0f;

                // Reject steep slopes (cliff faces) - crude finite-difference estimate, cheap and
                // good enough for v1 (no real physical placement rules yet).
                f32 slope = std::abs(h_dx - h_center) + std::abs(h_dz - h_center);
                if (slope > 0.02f) { continue; }

                f32 world_x = cell_v * static_cast<f32>(chunk_scale) + static_cast<f32>(position.x) * static_cast<f32>(chunk_scale);
                f32 world_z = cell_u * static_cast<f32>(chunk_scale) + static_cast<f32>(position.y) * static_cast<f32>(chunk_scale);
                f32 world_y = h_center * GTerrainPass.Config.HeightScale;

                // Trees on gentle mid-elevation ground, rocks higher/steeper - reuses the height
                // value already sampled instead of adding a new noise generator.
                PropModel model = (h_center > 0.55f) ? PropModel::Rock : PropModel::Tree;

                out.Instances[out.Count] = {
                    .WorldPosition = { world_x, world_y, world_z },
                    .YRotation = rotation_dist(rng),
                    .Scale = scale_dist(rng),
                    .Model = model
                };
                out.Count++;
            }
        }
    }
}
