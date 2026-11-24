#pragma once

#include <string>

#include "Core/Math.hpp"

struct Camera;

// Unattended screenshot runs, documented in docs/screenshots.md. Inactive (and every hook a
// no-op) unless --screenshots=<dir> is on the command line. Two shot sets:
//
// Stages (--shot-set=stages, default; shots.cmd) - one fixed view, one PNG per rendering stage:
//   1_terrain_debug       chunk debug colors, no props, no post pass
//   2_terrain_textured    procedural terrain shading, no props, no post pass
//   3_terrain_props       + instanced props
//   4_terrain_props_post  + PostPass (exposure + ACES tonemap) - the normal final image
//
// Chunk (--shot-set=chunk; chunkshot.cmd) - one ring 0 chunk, three equally sized square images:
//   1_heightmap           its heightmap, top-down, stretched to its own min..max
//   2_chunk_isolated      only that chunk drawn, procedural shading
//   3_chunk_in_context    same camera, whole world, with props
//
// Layers (--shot-set=layers; layers.cmd) - top-down maps of the terrain generator's layers over
// one square region, evaluated on the CPU straight from the generator (nothing is rendered, the
// run closes right after Init()):
//   1_continentes / 2_mascara_cordilheiras / 3_montanhas / 4_final (.png), absolute/ (same maps
//   on one shared 0..1 scale), layers_info.txt
//
// LOD rings (--shot-set=lod; lodshot.cmd) - the five concentric LOD rings, chunks tinted by ring
// (TerrainPass::RingColors), both from one streamed world centered on LodPosition:
//   1_aneis_topo          orthographic top-down over the whole coverage disk, chunk outlines,
//                         camera marker, no fog
//   2_aneis_wireframe     player-height perspective toward the horizon, triangle wireframe over
//                         the ring-tinted shading, fog on
namespace Screenshots {
    enum class ShotSet : u8 { Stages, Chunk, Layers, Lod };

    struct ShotConfig {
        std::string OutDir;             // empty = normal interactive run
        ShotSet Set = ShotSet::Stages;

        // Stages view. Defaults: low over the beach at the origin, looking across the lake
        // (default seed).
        f32 X = 0.0f;                   // world XZ of the camera
        f32 Z = 0.0f;
        f32 Altitude = 25.0f;           // above the visible surface (terrain or water) at X/Z
        f32 Yaw = 180.0f;               // degrees, 0 = looking along +X
        f32 Pitch = -8.0f;              // degrees, negative = looking down

        // Chunk view: the camera orbits the chunk's center at ChunkDistance, looking at it with
        // the same yaw/pitch convention as above, through a narrower lens than the game's 90
        // degrees - fills the frame with one chunk with far less perspective distortion.
        // Default: a chunk with some visible relief on the default seed (most ring 0 chunks are
        // close to a plain slope - 100 units is small next to the generator's features).
        ivec2 Chunk = { 3, 2 };         // ring 0 chunk grid position (world = grid * ChunkScale)
        f32 ChunkYaw = 45.0f;
        f32 ChunkPitch = -30.0f;
        f32 ChunkDistance = 260.0f;
        f32 ChunkFov = 40.0f;           // vertical, degrees
        // Side of all three square images. The window is created at this size for the chunk
        // set (--shot-width/--shot-height are ignored there) and the heightmap is nearest-
        // upscaled to match - a multiple of 64 keeps every texel the same pixel size.
        u32 ChunkImageSize = 1024;

        // Layers region: a LayersSize square centered on LayersCenter (world XZ), sampled at
        // LayersResolution^2 points. Default: about half the streamed area's width around the
        // origin - large enough for several continents, small enough that their shapes read.
        vec2 LayersCenter = { 0.0f, 0.0f };
        f32 LayersSize = 16000.0f;
        u32 LayersResolution = 1024;

        // LOD ring views. Both share LodPosition's XZ, so both show the same ring layout (streaming
        // only depends on the camera's XZ). The wireframe view stands LodAltitude above the
        // surface; its narrower-than-game lens makes far triangles big enough on screen for the
        // wireframe to stay readable. The top-down view is orthographic, LodTopHalfHeight world
        // units from center to the top edge - by default the whole coverage disk plus a margin
        // (0 = derived from TerrainManager::TotalCoverageRadius).
        // Default: on low land, looking up a valley with mountains 3-9 km out (default seed).
        vec2 LodPosition = { 500.0f, -1750.0f };
        f32 LodAltitude = 60.0f;
        f32 LodYaw = 90.0f;
        f32 LodPitch = -6.0f;
        f32 LodFov = 35.0f;
        f32 LodTopHalfHeight = 0.0f;
        f32 LodTopSunElevation = 60.0f; // higher than the wireframe view's: less slope shading,
                                        // so each band reads as one tone
        // PostPass exposure for the top-down view. At 1.0 the high sun lifts the light bands into
        // the tonemap's shoulder, where rings 3 and 4 end up only ~6 L* apart; lower keeps the
        // rendered bands close to TerrainPass::RingColors' even lightness steps.
        f32 LodTopExposure = 0.5f;

        f32 SunAzimuth = 30.0f;         // degrees, same convention as the Sky debug window
        f32 SunElevation = 35.0f;
        u32 Width = 1920;
        u32 Height = 1080;
    };

    inline bool Active = false;
    inline ShotConfig Config;

    // Consumes --screenshots=<dir> and every --shot-<key>=<value>, compacting argv/argc so the
    // remaining arguments can go on to Benchmark::ParseArgs, which rejects keys it doesn't know.
    // Returns false (and logs) on an unknown --shot- key or a malformed value.
    bool ParseArgs(i32& argc, char** argv);

    // Game::Create(), after TerrainManager::Init() (the camera height samples the generator).
    void Init(Camera& camera);

    // Game::Run(), in place of FlyByCamera::Update() - same ordering constraint before
    // RefreshChunks().
    void Update(f32 delta_time);

    // main(), after Game::Run(): whether every file of the shot set was written.
    bool Succeeded();
}
