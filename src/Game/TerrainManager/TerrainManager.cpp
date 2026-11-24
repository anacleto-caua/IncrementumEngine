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
#include "Engine/Core/FrameProfile.hpp"

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
    f32 ElevationImpl(f32 world_x, f32 world_z, f32 min_wavelength, const GeneratorParams& params, ElevationLayers* out_layers);


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

    // --- Ring ownership ---
    //
    // Every ring scans a lattice disc of RingScanRadius() chunks around the player's chunk at its
    // own scale, and the rings tile the ground as a quadtree: a ring i + 1 chunk is exactly a 2x2
    // block of ring i chunks (RingScalesDouble()). A block is drawn by ring i when all four of its
    // chunks are inside ring i's disc, and as the single ring i + 1 chunk otherwise. Every point is
    // drawn by exactly one ring - no gaps, and no double coverage z-fighting along the seams.
    //
    // This replaced a smooth-circle exclusion (skip outer chunks whose min corner was inside the
    // inner ring's radius minus one chunk). Measuring from the min corner made it lopsided - one
    // side's overlap band was ~3 chunks wide - and ~17% of the area within 13.5 km ended up drawn
    // by two rings at once, which z-fought and pushed outer rings past their draw budget whenever
    // nothing was culled.

    // `radius` defaults to the ring's own RingScanRadius(); NearOwnsChunk() widens/narrows it.
    bool InScanDisc(u32 ring_index, ivec2 chunk, vec3 player_position, i64 radius) {
        f64 scale = RingChunkScale(ring_index);
        i64 dx = static_cast<i64>(chunk.x) - static_cast<i64>(std::floor(static_cast<f64>(player_position.x) / scale));
        i64 dz = static_cast<i64>(chunk.y) - static_cast<i64>(std::floor(static_cast<f64>(player_position.z) / scale));
        return dx * dx + dz * dz <= radius * radius;
    }

    bool InScanDisc(u32 ring_index, ivec2 chunk, vec3 player_position) {
        return InScanDisc(ring_index, chunk, player_position, static_cast<i64>(RingScanRadius(ring_index)));
    }

    // Whether all four ring `ring_index` chunks making up ring ring_index + 1's chunk `block` are
    // inside ring_index's disc.
    bool BlockInScanDisc(u32 ring_index, ivec2 block, vec3 player_position, i64 radius) {
        for (i32 i = 0; i < 2; i++) {
            for (i32 j = 0; j < 2; j++) {
                if (!InScanDisc(ring_index, { block.x * 2 + i, block.y * 2 + j }, player_position, radius)) { return false; }
            }
        }
        return true;
    }

    bool BlockInScanDisc(u32 ring_index, ivec2 block, vec3 player_position) {
        return BlockInScanDisc(ring_index, block, player_position, static_cast<i64>(RingScanRadius(ring_index)));
    }

    // `chunk` must already be inside ring_index's own scan disc.
    bool OwnsChunk(u32 ring_index, ivec2 chunk, vec3 player_position) {
        // Whole block inside the next ring in - drawn there (or further in) instead.
        if (ring_index > 0 && BlockInScanDisc(ring_index - 1, chunk, player_position)) { return false; }
        // Part of a block the next ring out draws as one chunk. >> 1 floors negatives too
        // (arithmetic shift, guaranteed since C++20).
        if (ring_index < OuterRingCount && !BlockInScanDisc(ring_index, { chunk.x >> 1, chunk.y >> 1 }, player_position)) { return false; }
        return true;
    }

    // OwnsChunk() with every boundary pushed HandoffMargin chunks outward: the inner ring's disc
    // shrunk, this ring's own disc grown. A superset of what this ring owns - everything it could
    // own after one boundary crossing - kept resident and prefetched, so a handoff usually finds
    // its data already there. Unlike OwnsChunk(), needs no precondition on `chunk`.
    bool NearOwnsChunk(u32 ring_index, ivec2 chunk, vec3 player_position) {
        constexpr i64 margin = HandoffMargin;
        if (ring_index > 0 &&
            BlockInScanDisc(ring_index - 1, chunk, player_position, static_cast<i64>(RingScanRadius(ring_index - 1)) - margin)) {
            return false;
        }
        i64 grown_radius = static_cast<i64>(RingScanRadius(ring_index)) + margin;
        if (ring_index < OuterRingCount) {
            return BlockInScanDisc(ring_index, { chunk.x >> 1, chunk.y >> 1 }, player_position, grown_radius);
        }
        return InScanDisc(ring_index, chunk, player_position, grown_radius);
    }

    // Ring 0 and the outer rings are different RingState instantiations - this lets ring-index
    // driven code (the draw-list traversal) reach either with one generic lambda.
    template<typename F>
    decltype(auto) WithRing(u32 ring_index, F&& visit) {
        if (ring_index == 0) { return visit(Ring0); }
        return visit(OuterRings[ring_index - 1]);
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
        Ring0.OuterRadius = ChunkScale * static_cast<f64>(ExplorationRadius);
        Ring0.LayerOffset = 0;

        f64 previous_outer_radius = Ring0.OuterRadius;
        u32 layer_cursor = MaxCachedChunks;
        for (u32 i = 0; i < OuterRingCount; i++) {
            OuterRings[i].ChunkScale = OuterRingChunkScales[i];
            // Reaches all the way to this ring's own OuterRadius, not just the annulus width -
            // see the comment on RingState::ScanRadius and on OuterRingScanRadius in the header.
            OuterRings[i].ScanRadius = OuterRingScanRadius;
            OuterRings[i].OuterRadius = previous_outer_radius + OuterRings[i].ChunkScale * static_cast<f64>(OuterRingExplorationRadius);
            OuterRings[i].LayerOffset = layer_cursor;

            previous_outer_radius = OuterRings[i].OuterRadius;
            layer_cursor += OuterRingMaxCachedChunks;
        }

        // Kickstart the valid data - ring 0 only, exactly as before this plan.
        auto init_batch_start = std::chrono::steady_clock::now();
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
                if ((dx * dx) + (dy * dy) <= r_squared && OwnsChunk(0, { x, y }, player_pos)) {
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
        InitBatchMs = std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - init_batch_start).count();

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
        auto compute_start = std::chrono::steady_clock::now();
        WriteHeightmap(generation->StagingData, generation->Position, generation->WorldStep, generation->Params);
        GeneratePropPlacements(
            generation->StagingProps,
            generation->StagingData,
            generation->Position,
            generation->WorldStep * static_cast<f64>(VerticesPerEdge - 1)
        );
        generation->ComputeMs = std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - compute_start).count();
        generation->Done.store(true, std::memory_order_release);
    }

    // Picks a cache slot to generate a new chunk into, within one ring: prefer one that's never
    // been used, else evict the least-recently-used slot. Also skips any slot already claimed by
    // another in-flight generation in this same ring's pool - without this, two generations kicked
    // off in the same frame could both target the same cache slot (invalidating it doesn't remove
    // it from consideration on its own; only tracking "is some pool entry already writing here"
    // does). Returns UINT32_MAX rather than evict a slot stamped this frame (owned, in the handoff
    // band, or drawn), so a cache too small for its resident set stops generating instead of
    // punching holes into what's on screen - the static_assert on the cache sizes in the header is
    // what keeps that from happening in practice.
    template<typename RingT>
    u32 PickSlotToGenerateInto(RingT& ring, u64 current_tick) {
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

        u32 oldest = UINT32_MAX;
        u64 oldest_tick = current_tick;
        for (u32 i = 0; i < count; i++) {
            if (is_claimed(i)) { continue; }
            if (ring.Cache[i].LastUsedTick < oldest_tick) {
                oldest_tick = ring.Cache[i].LastUsedTick;
                oldest = i;
            }
        }
        return oldest;
    }

    // --- RefreshChunks(), stage 1: per-ring scan ---
    //
    // Stamps every resident chunk this ring wants this frame (owned or in its handoff band) as
    // recently used, counts owned-but-missing chunks, and ranks generation candidates into
    // ring.Candidates. Draws nothing - BuildDrawList() decides that across every ring at once.
    template<typename RingT>
    void ScanRing(RingT& ring, u32 ring_index, vec3 player_position, const Frustum& camera_frustum, u64 current_tick) {
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_position.x / ring.ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_position.z / ring.ChunkScale));

        // The handoff band reaches up to HandoffMargin chunks past the ring's own scan disc.
        i32 radius = static_cast<i32>(ring.ScanRadius + HandoffMargin);
        i32 r_squared = radius * radius;

        // The ring's own PoolCapacity best candidates found this pass, kept sorted ascending by
        // priority (index 0 = best) as the scan runs - NOT the first PoolCapacity found in scan
        // order. Visible (per the same frustum test the draw list uses) beats not-visible
        // outright; then a truly missing owned chunk beats a handoff prefetch, which beats
        // refreshing a stale one; ties go to the closer one - taking scan-order candidates instead
        // meant a region late in the raster scan could sit unresolved for many frames even while
        // directly in view.
        auto is_better = [](const GenerationCandidate& a, const GenerationCandidate& b) {
            if (a.Visible != b.Visible) { return a.Visible; }
            if (a.Kind != b.Kind) { return a.Kind < b.Kind; }
            return a.DistSq < b.DistSq;
        };
        ring.CandidateCount = 0;
        u32 missing_count = 0;

        auto consider_for_generation = [&](ivec2 candidate, bool visible, CandidateKind kind, i32 dx, i32 dy) {
            for (const PendingGeneration& gen : ring.GenerationPool) {
                if (gen.InFlight && gen.Position == candidate) { return; }
            }

            GenerationCandidate candidate_info = {
                .Position = candidate,
                .Visible = visible,
                .Kind = kind,
                .DistSq = static_cast<i64>(dx) * dx + static_cast<i64>(dy) * dy
            };

            // Insertion into the small (K=RingT::PoolCapacity) sorted buffer - cheaper and more
            // legible than pulling in <algorithm> for a size this small.
            u32 insert_at;
            if (ring.CandidateCount < RingT::PoolCapacity) {
                insert_at = ring.CandidateCount;
                ring.CandidateCount++;
            } else if (is_better(candidate_info, ring.Candidates[RingT::PoolCapacity - 1])) {
                insert_at = RingT::PoolCapacity - 1;
            } else {
                return;
            }
            while (insert_at > 0 && is_better(candidate_info, ring.Candidates[insert_at - 1])) {
                ring.Candidates[insert_at] = ring.Candidates[insert_at - 1];
                insert_at--;
            }
            ring.Candidates[insert_at] = candidate_info;
        };

        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {
                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;
                if ((dx * dx) + (dy * dy) > r_squared) { continue; }

                ivec2 candidate = { x, y };

                // Ring ownership is exclusive: a block another ring draws is skipped outright -
                // unless it's within this ring's handoff band, where it's kept resident (and
                // prefetched) without being counted as missing.
                bool owned = InScanDisc(ring_index, candidate, player_position) && OwnsChunk(ring_index, candidate, player_position);
                if (!owned && !(HandoffPrefetchEnabled && NearOwnsChunk(ring_index, candidate, player_position))) { continue; }

                bool visible = !CullingPrioritizesGeneration || Intersects(camera_frustum, ChunkBounds(candidate, ring.ChunkScale));

                auto slot_it = ring.PositionToSlot.find(PackPosition(candidate));
                if (slot_it != ring.PositionToSlot.end()) {
                    // Cache hit - always mark recently used regardless of visibility, so turning
                    // away from a chunk doesn't make it LRU-evict while it's still in range.
                    ring.Cache[slot_it->second].LastUsedTick = current_tick;
                    if (ring.Cache[slot_it->second].Epoch != GeneratorEpoch) {
                        consider_for_generation(candidate, visible, CandidateKind::Stale, dx, dy);
                    }
                } else {
                    if (owned) { missing_count++; }
                    consider_for_generation(candidate, visible, owned ? CandidateKind::Missing : CandidateKind::Prefetch, dx, dy);
                }
            }
        }

        ring.DebugStats.MissingLastFrame = missing_count;
    }

    // --- RefreshChunks(), stage 2: the draw list, across every ring at once ---
    //
    // The rings' ownership is a quadtree cut: starting from ring OuterRingCount's chunks, a node
    // splits into its 4 ring - 1 children exactly where OwnsChunk() hands the block inward. Walking
    // that tree (instead of each ring drawing what it owns on its own) is what lets a block whose
    // ideal owner isn't resident yet borrow another ring's data, instead of going dark:
    //  - Inward handoff (a block just became the finer ring's): if the children can't all be
    //    covered, the coarser parent is drawn in their place, as long as it's still resident.
    //  - Outward handoff (a block just became the coarser ring's): if the parent is missing, its
    //    resident finer children are drawn in its place.
    // Never both for the same area, so fallbacks can't double-cover or z-fight. Fallback data is
    // only there because ScanRing() keeps each ring's handoff band resident.

    struct DrawEntry {
        u32 Ring;
        ivec2 Position;   // grid position at Ring's own scale
        u32 Slot;         // local cache slot within Ring; NoDrawSlot = a hole nothing could cover
        bool Fallback;    // drawn by a ring that doesn't own this area right now
    };
    constexpr u32 NoDrawSlot = UINT32_MAX;

    // Reused every frame; entries get appended and rolled back during the traversal, then turned
    // into ChunkDrawList entries (frustum culling, stats) in one pass afterwards.
    std::vector<DrawEntry> DrawEntries;

    u32 ResidentSlot(u32 ring_index, ivec2 chunk) {
        return WithRing(ring_index, [&](auto& ring) -> u32 {
            auto found = ring.PositionToSlot.find(PackPosition(chunk));
            return found == ring.PositionToSlot.end() ? NoDrawSlot : found->second;
        });
    }

    ivec2 ChildChunk(ivec2 node, i32 index) {
        return { node.x * 2 + (index & 1), node.y * 2 + (index >> 1) };
    }

    // Appends whatever covers `node`'s area to DrawEntries. Returns whether that area is fully
    // covered, i.e. no hole entry was left in it.
    bool EmitDrawNode(u32 ring_index, ivec2 node, vec3 player_position) {
        bool split = ring_index > 0 && BlockInScanDisc(ring_index - 1, node, player_position);

        if (split) {
            size_t mark = DrawEntries.size();
            bool covered = true;
            for (i32 i = 0; i < 4; i++) {
                covered &= EmitDrawNode(ring_index - 1, ChildChunk(node, i), player_position);
            }
            if (covered || !HandoffFallbackEnabled) { return covered; }

            // A hole somewhere below - draw this node whole instead, if it's still resident.
            u32 slot = ResidentSlot(ring_index, node);
            if (slot != NoDrawSlot) {
                DrawEntries.resize(mark);
                DrawEntries.push_back({ .Ring = ring_index, .Position = node, .Slot = slot, .Fallback = true });
                return true;
            }
            return false; // best effort: keep whatever the children could cover
        }

        u32 slot = ResidentSlot(ring_index, node);
        if (slot != NoDrawSlot) {
            DrawEntries.push_back({ .Ring = ring_index, .Position = node, .Slot = slot, .Fallback = false });
            return true;
        }

        // Missing - fall back to whichever of the finer children are resident.
        bool covered = ring_index > 0 && HandoffFallbackEnabled;
        if (covered) {
            for (i32 i = 0; i < 4; i++) {
                ivec2 child = ChildChunk(node, i);
                u32 child_slot = ResidentSlot(ring_index - 1, child);
                if (child_slot == NoDrawSlot) {
                    covered = false;
                    continue;
                }
                DrawEntries.push_back({ .Ring = ring_index - 1, .Position = child, .Slot = child_slot, .Fallback = true });
            }
        }
        if (!covered) {
            DrawEntries.push_back({ .Ring = ring_index, .Position = node, .Slot = NoDrawSlot, .Fallback = false });
        }
        return covered;
    }

    // Must run before any of this frame's generations are finalized, so a chunk never gets drawn
    // in the same frame its GPU upload is queued: that upload's release/write/acquire chain hasn't
    // had a chance to run yet. Waiting until next frame gives AcquirePending (called every frame
    // from Renderer::Frame()) a full cycle to acquire it and for the draw's own
    // wait-on-last-acquire to cover it.
    void BuildDrawList(vec3 player_position, const Frustum& camera_frustum, u64 current_tick) {
        DrawEntries.clear();

        constexpr u32 root_ring = OuterRingCount;
        f64 root_scale = RingChunkScale(root_ring);
        ivec2 player_coord = {
            static_cast<i32>(std::floor(player_position.x / root_scale)),
            static_cast<i32>(std::floor(player_position.z / root_scale))
        };
        i32 radius = static_cast<i32>(RingScanRadius(root_ring));
        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {
                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;
                if ((dx * dx) + (dy * dy) > radius * radius) { continue; }
                EmitDrawNode(root_ring, { x, y }, player_position);
            }
        }

        for (u32 r = 0; r <= OuterRingCount; r++) {
            WithRing(r, [](auto& ring) {
                ring.DebugStats.DrawnLastFrame = 0;
                ring.DebugStats.CulledLastFrame = 0;
                ring.DebugStats.FallbackLastFrame = 0;
                ring.DebugStats.VisibleMissingLastFrame = 0;
            });
        }

        u32 draw_cursor = 0;
        for (const DrawEntry& entry : DrawEntries) {
            WithRing(entry.Ring, [&](auto& ring) {
                bool intersects = Intersects(camera_frustum, ChunkBounds(entry.Position, ring.ChunkScale));

                if (entry.Slot == NoDrawSlot) {
                    if (!DrawCullingEnabled || intersects) { ring.DebugStats.VisibleMissingLastFrame++; }
                    return;
                }

                // A fallback chunk may sit outside its ring's handoff band - keep it from being
                // evicted by this frame's kick-offs all the same.
                ring.Cache[entry.Slot].LastUsedTick = current_tick;

                bool isolated_out = IsolateChunk && (entry.Ring != 0 || entry.Position != IsolatedChunk);
                if ((!DrawCullingEnabled || intersects) && !isolated_out && draw_cursor < TotalMaxDrawnChunks) {
                    ChunkDrawList[draw_cursor] = {
                        .WorldPos = entry.Position,
                        .TextureLayer = entry.Slot + ring.LayerOffset,
                        .Scale = static_cast<f32>(ring.ChunkScale)
                    };
                    draw_cursor++;
                    ring.DebugStats.DrawnLastFrame++;
                    if (entry.Fallback) { ring.DebugStats.FallbackLastFrame++; }
                } else {
                    ring.DebugStats.CulledLastFrame++;
                }
            });
        }
        CurrentlyActiveChunks = draw_cursor;
    }

    // --- RefreshChunks(), stage 3: per-ring finalize + kick-off ---
    //
    // Returns true if this ring queued any completed chunk's slice this call - the caller batches
    // this across every ring into one SubmitReleaseAndWrite() call per frame; a per-ring call
    // point hit a real GPU-timing assertion failure once the pool let several rings each queue
    // several chunks in close succession.
    template<typename RingT>
    bool FinalizeGenerations(RingT& ring, u32 ring_index, u64 current_tick) {
        bool any_finalized = false;
        for (PendingGeneration& gen : ring.GenerationPool) {
            if (!gen.InFlight || !gen.Done.load(std::memory_order_acquire)) { continue; }

            std::memcpy(ring.HeightmapData[gen.TargetLayer], gen.StagingData, sizeof(Heightmap));
            ring.Props[gen.TargetLayer] = gen.StagingProps;

            ring.DebugStats.ChunksGenerated++;
            ring.DebugStats.LastGenerationMs =
                std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - gen.StartTime).count();
            if (RecordGenerations) {
                GenerationLog.push_back({ .Ring = ring_index, .ComputeMs = gen.ComputeMs, .LatencyMs = ring.DebugStats.LastGenerationMs });
            }

            // A stale-chunk regeneration lands in a fresh slot while the old one was still being
            // drawn (this very frame included - the draw list is already built). The old slot is
            // orphaned rather than freed: it stays Valid with its current LastUsedTick but leaves
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
        return any_finalized;
    }

    // Kicks off generation for ScanRing()'s best candidates, up to GenerationPoolSize
    // concurrently instead of strictly one at a time.
    template<typename RingT>
    void KickOffGenerations(RingT& ring, u64 current_tick) {
        for (u32 m = 0; m < ring.CandidateCount; m++) {
            PendingGeneration* free_slot = nullptr;
            for (PendingGeneration& gen : ring.GenerationPool) {
                if (!gen.InFlight) { free_slot = &gen; break; }
            }
            if (!free_slot) { break; } // every pool slot busy - the rest wait for next frame

            u32 target_layer = PickSlotToGenerateInto(ring, current_tick);
            if (target_layer == UINT32_MAX) { break; } // nothing evictable this frame

            const GenerationCandidate& candidate = ring.Candidates[m];
            PendingGeneration& gen = *free_slot;
            gen.Position = candidate.Position;
            gen.TargetLayer = target_layer;
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
                // Only unmap if the map still points here - an orphaned stale slot (see
                // FinalizeGenerations()) shares its Position with the fresh slot that replaced it,
                // and that mapping must survive this eviction.
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

            // The pool-slot reservation above prioritizes which candidate gets a slot, but says
            // nothing about the order TaskScheduler's worker threads actually run submitted work
            // in. TaskPriority::High routes a currently-visible chunk into a separate,
            // always-drained-first queue tier so it can't get stuck behind older,
            // now-less-relevant submissions under a big enough burst.
            TaskScheduler::SubmitTask(
                GenerateHeightmapTask,
                &gen,
                candidate.Visible ? TaskScheduler::TaskPriority::High : TaskScheduler::TaskPriority::Normal
            );
        }

        u32 in_flight_count = 0;
        for (const PendingGeneration& gen : ring.GenerationPool) { if (gen.InFlight) { in_flight_count++; } }
        ring.DebugStats.GenerationsInFlight = in_flight_count;
    }

    void Regenerate() {
        GeneratorEpoch++;
    }

    void RefreshChunks(vec3 player_position, const Frustum& camera_frustum) {
        auto profile_start = FrameProfile::Clock::now();
        CurrentTick++;

        ScanRing(Ring0, 0, player_position, camera_frustum, CurrentTick);
        for (u32 i = 0; i < OuterRingCount; i++) {
            ScanRing(OuterRings[i], i + 1, player_position, camera_frustum, CurrentTick);
        }

        BuildDrawList(player_position, camera_frustum, CurrentTick);

        bool any_finalized = FinalizeGenerations(Ring0, 0, CurrentTick);
        KickOffGenerations(Ring0, CurrentTick);
        for (u32 i = 0; i < OuterRingCount; i++) {
            any_finalized |= FinalizeGenerations(OuterRings[i], i + 1, CurrentTick);
            KickOffGenerations(OuterRings[i], CurrentTick);
        }

        FrameProfile::Current.TransferSubmitMs = 0.0f;
        FrameProfile::Current.TransferReleaseMs = 0.0f;
        FrameProfile::Current.TransferRecordMs = 0.0f;
        FrameProfile::Current.TransferQueueSubmitMs = 0.0f;
        if (any_finalized) {
            auto submit_start = FrameProfile::Clock::now();
            GTransferPipe.SubmitReleaseAndWrite();
            FrameProfile::Current.TransferSubmitMs = FrameProfile::MsSince(submit_start);
        }
        FrameProfile::Current.TerrainMs = FrameProfile::MsSince(profile_start);
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
        return ElevationImpl(world_x, world_z, min_wavelength, params, nullptr);
    }

    ElevationLayers SampleElevationLayers(f32 world_x, f32 world_z, f32 min_wavelength) {
        ElevationLayers layers;
        ElevationImpl(world_x, world_z, min_wavelength, Generator, &layers);
        return layers;
    }

    // out_layers (diagnostic export only) additionally evaluates the mountain field where the mask
    // is zero, which the normal path skips - otherwise the result is identical either way.
    f32 ElevationImpl(f32 world_x, f32 world_z, f32 min_wavelength, const GeneratorParams& params, ElevationLayers* out_layers) {
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
        if (massif > 0.0f || out_layers) {
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

            f32 field = 0.2f + 0.8f * shaped + params.PeakSharpness * 0.3f * crest * peaks * peaks;
            f32 raw = massif * std::sqrt(massif) * field;
            // Soft ceiling (1 - e^-kx, normalized to reach MountainHeight at raw = 1) instead of a
            // hard clamp, so the very tallest peaks round off rather than flatten.
            constexpr f32 Softness = 2.2f;
            auto soft_ceiling = [&](f32 x) {
                return params.MountainHeight * (1.0f - std::exp(-Softness * x)) / (1.0f - std::exp(-Softness));
            };
            mountains = soft_ceiling(raw);
            if (out_layers) { out_layers->Mountains = soft_ceiling(field); }
        }

        f32 elevation = std::clamp(base + hills + mountains, 0.0f, 1.0f);
        if (out_layers) {
            out_layers->Continents = base;
            out_layers->RangeMask = massif;
            out_layers->Hills = hills;
            out_layers->Final = elevation;
        }
        return elevation;
    }

    bool IsWorldComplete() {
        auto ring_complete = [](const Stats& stats) {
            return stats.GenerationsInFlight == 0 && stats.MissingLastFrame == 0;
        };
        if (!ring_complete(Ring0.DebugStats)) { return false; }
        for (const OuterRingState& ring : OuterRings) {
            if (!ring_complete(ring.DebugStats)) { return false; }
        }
        return true;
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
