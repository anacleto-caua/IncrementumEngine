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
    void WriteHeightmap(Heightmap& out, ivec2 position, f64 world_step, const GeneratorParams& params);
    void GeneratePropPlacements(PropPlacement& out, const Heightmap& heightmap, ivec2 position, f64 chunk_scale);

    // Terrain generator noise - configured once in Init(), read-only afterwards (worker threads
    // sample these concurrently; FastNoiseLite's GetNoise/DomainWarp are const-safe).
    FastNoiseLite ContinentWarp;     // domain warp bending coastlines/ranges into organic shapes
    FastNoiseLite ContinentalNoise;  // ocean vs land and broad plateau height, very low frequency
    FastNoiseLite RangeNoise;        // where mountain massifs sit (broad regions, not every hill)
    FastNoiseLite MountainNoise;     // single-octave base - ErodedFbm() sums its octaves by hand
    FastNoiseLite HillNoise;         // gentle rolling relief on open land

    f32 Elevation(f32 world_x, f32 world_z, f32 min_wavelength, const GeneratorParams& params);


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
        WriteHeightmap(Ring0.HeightmapData[task->TargetLayer], task->Position, task->WorldStep, Generator);
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
        // Frequencies are in world units (1 / wavelength): continents span thousands of units to
        // fill the ~16000-unit view, massifs ~4000, mountain base ~2500, hills ~300.
        //
        // Warp is kept moderate on purpose: a strong warp (tried: amp 1300, 3 progressive octaves)
        // compresses space in places, squeezing the range mask's rise into a few hundred units
        // and producing sheer "wall" mountains.
        ContinentWarp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
        ContinentWarp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
        ContinentWarp.SetFractalOctaves(2);
        ContinentWarp.SetFrequency(0.00025f);
        // Unit amplitude: Elevation() scales the resulting displacement by
        // GeneratorParams::WarpStrength itself, so the strength stays live-tunable without
        // touching this (shared, read-only-after-Init) noise object.
        ContinentWarp.SetDomainWarpAmp(1.0f);

        ContinentalNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ContinentalNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        ContinentalNoise.SetFractalOctaves(5);
        ContinentalNoise.SetFrequency(0.00022f);
        ContinentalNoise.SetSeed(1337);

        RangeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        RangeNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        RangeNoise.SetFractalOctaves(2);
        RangeNoise.SetFrequency(0.00025f);
        RangeNoise.SetSeed(4242);

        MountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        MountainNoise.SetFractalType(FastNoiseLite::FractalType_None);
        MountainNoise.SetFrequency(1.0f); // ErodedFbm() scales coordinates itself, per octave
        MountainNoise.SetSeed(9001);

        HillNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        HillNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        HillNoise.SetFractalOctaves(3);
        HillNoise.SetFrequency(0.0033f);
        HillNoise.SetSeed(777);

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
        WriteHeightmap(generation->StagingData, generation->Position, generation->WorldStep, generation->Params);
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
        //
        // Stale chunks (cached, but generated under an older GeneratorEpoch - see Regenerate())
        // compete for the same pool slots, ranked just below a truly missing chunk of the same
        // visibility: a dark hole always beats refreshing terrain that's already on screen.
        struct PrioritizedMiss {
            ivec2 Position;
            bool Visible;
            bool Stale;
            i64 DistSq;   // chunk-grid units, this ring's own scale - only compared within this ring
        };
        auto is_better = [](const PrioritizedMiss& a, const PrioritizedMiss& b) {
            if (a.Visible != b.Visible) { return a.Visible; }
            if (a.Stale != b.Stale) { return !a.Stale; }
            return a.DistSq < b.DistSq;
        };
        std::array<PrioritizedMiss, RingT::PoolCapacity> best_missing;
        u32 best_count = 0;
        u32 culled_count = 0;
        u32 drawn_count = 0;
        u32 visible_missing_count = 0;

        auto consider_for_generation = [&](ivec2 candidate, bool visible, bool stale, i32 dx, i32 dy) {
            for (const PendingGeneration& gen : ring.GenerationPool) {
                if (gen.InFlight && gen.Position == candidate) { return; }
            }

            PrioritizedMiss candidate_info = {
                .Position = candidate,
                .Visible = visible,
                .Stale = stale,
                .DistSq = static_cast<i64>(dx) * dx + static_cast<i64>(dy) * dy
            };

            // Insertion into the small (K=RingT::PoolCapacity) sorted buffer - cheaper and more
            // legible than pulling in <algorithm> for a size this small.
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
        };

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

                    if (ring.Cache[cache_index].Epoch != GeneratorEpoch) {
                        consider_for_generation(candidate, !CullingEnabled || intersects, true, dx, dy);
                    }
                } else {
                    // Visible-but-missing counts regardless of already-in-flight status below -
                    // an in-flight chunk is still visibly dark on screen until it actually finishes,
                    // so it belongs in this "how many dark chunks are in view right now" stat too.
                    bool visible = !CullingEnabled || Intersects(camera_frustum, ChunkBounds(candidate, ring.ChunkScale));
                    if (visible) { visible_missing_count++; }

                    consider_for_generation(candidate, visible, false, dx, dy);
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

            // A stale-chunk regeneration lands in a fresh slot while the old one was still being
            // drawn (this very frame included - phase 1 already ran). The old slot is orphaned
            // rather than freed: it stays Valid with its current LastUsedTick but leaves
            // PositionToSlot, so nothing draws it from next frame on, and LRU only reclaims it once
            // it's genuinely the oldest slot - instead of PickSlotToGenerateInto() grabbing it (as
            // an invalid slot) immediately and uploading over a layer frames in flight may still
            // be sampling.
            ring.PositionToSlot.erase(PackPosition(gen.Position));

            ring.Cache[gen.TargetLayer] = {
                .Position = gen.Position,
                .Valid = true,
                .LastUsedTick = current_tick,
                .Epoch = gen.Epoch
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
            gen.Params = Generator;
            gen.Epoch = GeneratorEpoch;

            // Invalidate the slot the instant it's claimed for reuse, not only once generation
            // finishes. Otherwise, for however many frames generation takes, this slot is still
            // a "valid" cache hit for whatever position it used to hold - if that old position
            // is still in range, it keeps getting drawn with data that's about to be silently
            // replaced by a completely unrelated position's terrain the moment finalize lands,
            // instead of cleanly showing as missing and getting regenerated on its own.
            if (ring.Cache[gen.TargetLayer].Valid) {
                // Only unmap if the map still points here - an orphaned stale slot (see phase 2)
                // shares its Position with the fresh slot that replaced it, and that mapping must
                // survive this eviction.
                auto mapped = ring.PositionToSlot.find(PackPosition(ring.Cache[gen.TargetLayer].Position));
                if (mapped != ring.PositionToSlot.end() && mapped->second == gen.TargetLayer) {
                    ring.PositionToSlot.erase(mapped);
                }

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

    void Regenerate() {
        GeneratorEpoch++;
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

    f32 SmoothStep(f32 edge0, f32 edge1, f32 x) {
        f32 t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Mountain shape: gradient FBm with derivative-based damping (Inigo Quilez's "erosion" FBm).
    // Each octave is divided by 1 + k*|slope so far|^2, so fine detail survives on crests and
    // valley floors but is suppressed on steep flanks - broad masses with smooth, gullied slopes.
    // Fully local (no neighborhood simulation), so chunk borders stay seamless. Returns ~[0,1].
    //
    // Two tuned-by-measurement details (see notes/terrain_generation.md):
    //  - The damping slope accumulates each octave's gradient weighted by its amplitude*frequency,
    //    i.e. the terrain's ACTUAL accumulated slope. Accumulating raw per-octave gradients (IQ's
    //    shader form) lets fine octaves flip the damping on/off every few units, which showed up as
    //    small crease/scratch artifacts all over the mountains.
    //  - Plain (not ridged) noise: ridged 1-|n| has a one-sample-wide cusp along every crest, which
    //    produced both knife-edge "wall" ridges and single-vertex needle spikes.
    //
    // Octaves approaching `min_wavelength` fade out (rather than hard-cutting) so an outer LOD ring,
    // sampling at up to ~25 units per texel, doesn't alias noise it can't represent. Amplitudes are
    // fixed per octave, so dropping fine octaves never rescales the mountain as a whole.
    f32 ErodedFbm(f32 x, f32 z, f32 min_wavelength, f32 base_wavelength, f32 slope_damping) {
        constexpr u32 Octaves = 8;
        constexpr f32 Gain = 0.45f;
        constexpr f32 Epsilon = 0.01f;       // finite-difference step, in noise-space units

        f32 sum = 0.0f;
        f32 amplitude = 0.5f;
        f32 frequency = 1.0f / base_wavelength;
        f32 slope_x = 0.0f;
        f32 slope_z = 0.0f;

        for (u32 i = 0; i < Octaves; i++) {
            f32 lod_fade = SmoothStep(min_wavelength, 2.0f * min_wavelength, 1.0f / frequency);
            if (lod_fade <= 0.0f) { break; }

            // Per-octave offset decorrelates octaves (otherwise they all share the origin's feature).
            f32 offset = static_cast<f32>(i) * 31.7f;
            f32 px = x * frequency + offset;
            f32 pz = z * frequency - offset;

            f32 n = MountainNoise.GetNoise(px, pz);
            f32 grad_x = (MountainNoise.GetNoise(px + Epsilon, pz) - n) / Epsilon;
            f32 grad_z = (MountainNoise.GetNoise(px, pz + Epsilon) - n) / Epsilon;

            f32 slope_weight = amplitude * frequency * base_wavelength;
            slope_x += grad_x * slope_weight;
            slope_z += grad_z * slope_weight;

            sum += amplitude * lod_fade * n / (1.0f + slope_damping * (slope_x * slope_x + slope_z * slope_z));

            amplitude *= Gain;
            frequency *= 2.0f;
        }
        // 0.75 (not a larger scale + clamp): clamping here flattened the tallest peaks into discs.
        return std::clamp(sum * 0.75f + 0.5f, 0.0f, 1.0f);
    }

    // Normalized [0,1] terrain height at one world position. Layered, largest to smallest:
    //   continents - warped very-low-frequency noise decides ocean vs land and broad plateau height
    //   hills      - gentle, low-amplitude rolling relief on land. There is deliberately NO
    //                high-frequency noise on open ground - that is what made plains look pitted.
    //   mountains  - ErodedFbm() masked to broad massifs. The mask ramps over thousands of units, so
    //                mountains rise out of foothills instead of standing up as sheer walls.
    f32 Elevation(f32 world_x, f32 world_z, f32 min_wavelength, const GeneratorParams& params) {
        // Seed = a large per-seed offset into the same noise field, rather than reseeding the
        // FastNoiseLite objects - those are shared with worker threads and stay read-only after
        // Init(). Offsets stay under ~40000 units so f32 coordinates keep sub-centimetre precision.
        if (params.Seed != 0) {
            u32 seed = static_cast<u32>(params.Seed);
            world_x += static_cast<f32>((seed * 7919u) % 80000u) - 40000.0f;
            world_z += static_cast<f32>((seed * 104729u) % 80000u) - 40000.0f;
        }

        f32 wx = world_x;
        f32 wz = world_z;
        ContinentWarp.DomainWarp(wx, wz);
        wx = world_x + (wx - world_x) * params.WarpStrength;
        wz = world_z + (wz - world_z) * params.WarpStrength;

        // LandBias keeps most of the world above water - ocean only where the noise dips well
        // below zero.
        f32 continent = ContinentalNoise.GetNoise(wx, wz) + params.LandBias;
        f32 inland = SmoothStep(0.02f, 0.45f, continent);
        f32 base = SeaLevel + continent * 0.13f;

        f32 range = RangeNoise.GetNoise(wx, wz) + params.MountainCoverage;
        f32 massif = SmoothStep(-0.3f, 0.7f, range) * inland;
        // Foothills: hills swell as they approach a massif, so mountains rise out of rougher ground
        // instead of popping straight out of a flat plain.
        f32 foothill = SmoothStep(-0.35f, 0.1f, range) * inland;

        f32 hills = (HillNoise.GetNoise(world_x, world_z) * 0.5f + 0.5f)
                  * (0.012f + 0.03f * foothill)
                  * (0.3f + 0.7f * inland)
                  * params.HillAmount;

        f32 mountains = 0.0f;
        if (massif > 0.0f) {
            f32 peaks = ErodedFbm(world_x, world_z, min_wavelength, params.MountainWavelength, params.ErosionStrength);

            // Peak sharpness, two parts (measured: no needles, >60 deg slopes stay ~1-2%):
            //  - a steeper power curve on the peak value: shoulders drop relative to the summit,
            //    normalized at p = 0.8 so a typical summit keeps its height;
            //  - a soft-crested ridge line added only high up (weighted by peaks^2). Soft crest =
            //    sqrt(n^2 + c^2) - c instead of |n|: the hard cusp of |n| is exactly what produced
            //    single-vertex needles before (see notes/terrain_generation.md).
            f32 sharpen = 2.5f * params.PeakSharpness;
            f32 shaped = std::pow(peaks, 2.0f + sharpen) / std::pow(0.8f, sharpen);
            f32 crest = 0.0f;
            if (params.PeakSharpness > 0.0f) {
                f32 crest_frequency = 1.0f / (params.MountainWavelength * 0.3f);
                f32 n = MountainNoise.GetNoise(world_x * crest_frequency + 500.0f, world_z * crest_frequency - 500.0f);
                constexpr f32 CrestSoftness = 0.1f;
                f32 soft_abs = std::sqrt(n * n + CrestSoftness * CrestSoftness) - CrestSoftness;
                crest = (1.0f - soft_abs) * (1.0f - soft_abs);
            }

            f32 raw = massif * std::sqrt(massif)
                    * (0.2f + 0.8f * shaped + params.PeakSharpness * 0.3f * crest * peaks * peaks);
            // Soft ceiling (1 - e^-kx, normalized to reach MountainHeight at raw = 1) instead of a
            // hard clamp, so the very tallest peaks round off rather than flatten.
            constexpr f32 Softness = 2.2f;
            mountains = params.MountainHeight * (1.0f - std::exp(-Softness * raw)) / (1.0f - std::exp(-Softness));
        }

        return std::clamp(base + hills + mountains, 0.0f, 1.0f);
    }

    f32 SampleSurfaceHeight(f32 world_x, f32 world_z) {
        f32 elevation = std::max(Elevation(world_x, world_z, 0.0f, Generator), SeaLevel);
        return elevation * GTerrainPass.Config.HeightScale;
    }

    void WriteHeightmap(Heightmap& out, ivec2 position, f64 world_step, const GeneratorParams& params) {
        i32 terrain_res = VerticesPerEdge;
        // Noise finer than ~2 texels can't be represented at this ring's sample spacing.
        f32 min_wavelength = static_cast<f32>(world_step * 2.0);

        f32 global_x, global_z;
        for (i32 x = 0; x < terrain_res; x++) {
            global_x = static_cast<f32>(static_cast<f64>(x + ((terrain_res-1) * position.x)) * world_step);
            for (i32 z = 0; z < terrain_res; z++) {
                global_z = static_cast<f32>(static_cast<f64>(z + ((terrain_res-1) * position.y)) * world_step);

                f32 elevation = Elevation(global_x, global_z, min_wavelength, params);
                out[x][z] = static_cast<u16>(elevation * 65535.0f);
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

                // Nothing grows underwater or on the beach right at the waterline.
                if (h_center < SeaLevel + 0.008f) { continue; }

                // Reject steep slopes (cliff faces) - rise over run in WORLD units, so the threshold
                // stays meaningful regardless of HeightScale or which ring's texel spacing this is.
                f32 texel_world_size = static_cast<f32>(chunk_scale) / static_cast<f32>(LastTexel);
                f32 rise = (std::abs(h_dx - h_center) + std::abs(h_dz - h_center)) * GTerrainPass.Config.HeightScale;
                if (rise / texel_world_size > 0.55f) { continue; }

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
