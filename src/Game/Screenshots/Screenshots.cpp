#include "Screenshots.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include <stb/stb_image_write.h>

#include "Game/Game.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"
#include "Engine/Core/PerfOverlay.hpp"
#include "Engine/Core/Platform.hpp"
#include "Renderer/Camera.hpp"
#include "Renderer/Tools/DebugPanel.hpp"

namespace Screenshots {
    // Fixed = the camera Init() placed stays put; the Lod views re-place it per shot.
    enum class ShotView : u8 { Fixed, LodTopDown, LodPlayer };

    struct Shot {
        const char* Name;
        bool ChunkDebugColors;
        PropPass::DrawMode Props;
        bool Tonemap;
        bool IsolateChunk;
        ShotView View;
        bool RingColors;
        bool ChunkOutlines;
        bool Wireframe;
        bool Fog;
    };

    // Name, chunk debug colors, props, tonemap, isolate chunk, view, ring colors, chunk outlines,
    // wireframe, fog.
    constexpr std::array<Shot, 4> StageShots = {{
        { "1_terrain_debug", true, PropPass::DrawMode::Off, false, false, ShotView::Fixed, false, false, false, true },
        { "2_terrain_textured", false, PropPass::DrawMode::Off, false, false, ShotView::Fixed, false, false, false, true },
        { "3_terrain_props", false, PropPass::DrawMode::Instanced, false, false, ShotView::Fixed, false, false, false, true },
        { "4_terrain_props_post", false, PropPass::DrawMode::Instanced, true, false, ShotView::Fixed, false, false, false, true },
    }};

    constexpr std::array<Shot, 2> ChunkShots = {{
        { "2_chunk_isolated", false, PropPass::DrawMode::Off, true, true, ShotView::Fixed, false, false, false, true },
        { "3_chunk_in_context", false, PropPass::DrawMode::Instanced, true, false, ShotView::Fixed, false, false, false, true },
    }};

    // Props off in both: at these scales they're noise over the ring bands.
    constexpr std::array<Shot, 2> LodShots = {{
        { "1_aneis_topo", false, PropPass::DrawMode::Off, true, false, ShotView::LodTopDown, true, true, false, false },
        { "2_aneis_wireframe", false, PropPass::DrawMode::Off, true, false, ShotView::LodPlayer, true, false, true, true },
    }};

    // The world counts as complete only after this many consecutive complete frames: the stats
    // are all zero before the first RefreshChunks(), which would otherwise read as complete.
    constexpr u32 CompleteFramesRequired = 30;
    constexpr f32 WorldWaitTimeout = 180.0f;
    // Before the first shot, once the world is complete - lets the window/present settle.
    constexpr f32 FirstShotSettle = 1.0f;
    // After changing a shot's toggles: Renderer::Frame() runs at the top of the next loop
    // iteration, so one frame is enough in principle - the margin covers per-frame uploads
    // (prop instance buffers) landing a frame late.
    constexpr u32 ShotSettleFrames = Renderer::MAX_FRAMES_IN_FLIGHT + 2;

    enum class Phase : u8 { WaitWorld, Settle, Capturing, Done };

    std::span<const Shot> Shots;
    u32 ExpectedFiles = 0;
    u32 FilesWritten = 0;

    Phase CurrentPhase = Phase::Done;
    f32 PhaseTime = 0.0f;
    u32 CompleteFrames = 0;
    u32 ShotIndex = 0;
    u32 ShotFrames = 0;
    bool Requested = false;

    // The game camera, for shots that move it (ShotView::Lod*), and its lens as Game::Create() set
    // it up, which the top-down view overrides.
    Camera* ShotCamera = nullptr;
    f32 DefaultNearPlane = 0.1f;
    f32 DefaultFarPlane = 1000.0f;

    std::filesystem::path OutPath(std::string_view file_name) {
        return std::filesystem::path(Config.OutDir) / file_name;
    }

    std::filesystem::path ShotPath(const Shot& shot) {
        return OutPath(std::string(shot.Name) + ".png");
    }

    vec3 FrontFromAngles(f32 yaw_degrees, f32 pitch_degrees) {
        f32 yaw = math::radians(yaw_degrees);
        f32 pitch = math::radians(pitch_degrees);
        return normalize(vec3(
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch)
        ));
    }

    // Both LOD views stand over the same XZ, so the rings (centered on the camera's XZ) are the
    // same in both and switching views doesn't make anything stream.
    void PlaceLodCamera(Camera& camera, ShotView view) {
        f32 x = Config.LodPosition.x;
        f32 z = Config.LodPosition.y;

        if (view == ShotView::LodTopDown) {
            f32 half_height = Config.LodTopHalfHeight > 0.0f
                ? Config.LodTopHalfHeight
                : static_cast<f32>(TerrainManager::TotalCoverageRadius) * 1.04f;
            // Above the highest the terrain can reach (normalized 1.0), far plane below sea floor 0.
            f32 top = GTerrainPass.Config.HeightScale + 100.0f;
            camera.Orthographic = true;
            camera.OrthoHalfHeight = half_height;
            camera.Position = vec3(x, top, z);
            camera.Front = vec3(0.0f, -1.0f, 0.0f);
            // lookAt needs an up that isn't parallel to Front: +Z up in the image, +X right.
            camera.Up = vec3(0.0f, 0.0f, 1.0f);
            camera.NearPlane = 1.0f;
            camera.FarPlane = top + 100.0f;
            GSkyPass.SunElevationDegrees = Config.LodTopSunElevation;
            GPostPass.Exposure = Config.LodTopExposure;
        } else {
            camera.Orthographic = false;
            camera.Fov = Config.LodFov;
            camera.Position = vec3(x, TerrainManager::SampleSurfaceHeight(x, z) + Config.LodAltitude, z);
            camera.Front = FrontFromAngles(Config.LodYaw, Config.LodPitch);
            camera.Up = vec3::UP;
            camera.NearPlane = DefaultNearPlane;
            camera.FarPlane = DefaultFarPlane;
            GSkyPass.SunElevationDegrees = Config.SunElevation;
            GPostPass.Exposure = 1.0f;
        }
        UpdateMatrices(camera);
    }

    void ApplyShot(const Shot& shot) {
        GTerrainPass.ShowChunkDebugColors = shot.ChunkDebugColors;
        GTerrainPass.ShowRingColors = shot.RingColors;
        GTerrainPass.ShowChunkOutlines = shot.ChunkOutlines;
        GTerrainPass.ShowWireframe = shot.Wireframe;
        GTerrainPass.FogEnabled = shot.Fog;
        GTerrainPass.ShowCameraMarker = shot.View == ShotView::LodTopDown;
        GPropPass.Mode = shot.Props;
        GPostPass.Tonemap = shot.Tonemap;
        TerrainManager::IsolateChunk = shot.IsolateChunk;
        TerrainManager::IsolatedChunk = Config.Chunk;
        if (shot.View != ShotView::Fixed) {
            PlaceLodCamera(*ShotCamera, shot.View);
        }
        ShotFrames = 0;
        Requested = false;
    }

    vec3 ChunkCenter() {
        f32 scale = static_cast<f32>(TerrainManager::ChunkScale);
        f32 x = (static_cast<f32>(Config.Chunk.x) + 0.5f) * scale;
        f32 z = (static_cast<f32>(Config.Chunk.y) + 0.5f) * scale;
        return vec3(x, TerrainManager::SampleSurfaceHeight(x, z), z);
    }

    // ---- Heightmap export ----

    // 8-bit grayscale, ChunkImageSize square, stretched low..high, nearest-upscaled. Pixel (px, py)
    // = texel for world X index px, world Z index py - a top-down map, +X right, +Z down.
    bool WriteHeightmapPng(const std::filesystem::path& path, const TerrainManager::Heightmap& heightmap, u16 low, u16 high) {
        constexpr u32 Res = TerrainManager::VerticesPerEdge;
        const u32 size = std::max<u32>(Config.ChunkImageSize, 1);
        f32 range = std::max(static_cast<f32>(high) - static_cast<f32>(low), 1.0f);

        std::vector<u8> pixels(static_cast<size_t>(size) * size);
        for (u32 py = 0; py < size; py++) {
            u32 texel_z = py * Res / size;
            for (u32 px = 0; px < size; px++) {
                u32 texel_x = px * Res / size;
                f32 t = (static_cast<f32>(heightmap[texel_x][texel_z]) - static_cast<f32>(low)) / range;
                pixels[static_cast<size_t>(py) * size + px] = static_cast<u8>(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
        i32 dimension = static_cast<i32>(size);
        return stbi_write_png(path.string().c_str(), dimension, dimension, 1, pixels.data(), dimension) != 0;
    }

    void ExportHeightmap() {
        auto slot_it = TerrainManager::Ring0.PositionToSlot.find(TerrainManager::PackPosition(Config.Chunk));
        if (slot_it == TerrainManager::Ring0.PositionToSlot.end() || !TerrainManager::Ring0.Cache[slot_it->second].Valid) {
            analog::error("screenshots: chunk ({}, {}) isn't resident in ring 0 - no heightmap export", Config.Chunk.x, Config.Chunk.y);
            return;
        }
        const TerrainManager::Heightmap& heightmap = TerrainManager::Ring0.HeightmapData[slot_it->second];

        u16 low = UINT16_MAX;
        u16 high = 0;
        for (const auto& row : heightmap) {
            for (u16 value : row) {
                low = std::min(low, value);
                high = std::max(high, value);
            }
        }

        if (WriteHeightmapPng(OutPath("1_heightmap.png"), heightmap, low, high)) { FilesWritten++; }
        analog::info("screenshots: exported heightmap of chunk ({}, {})", Config.Chunk.x, Config.Chunk.y);
    }

    // ---- Generator layer export ----

    struct LayerDesc {
        const char* Name;
        const char* Description;
        f32 TerrainManager::ElevationLayers::* Field;
    };

    constexpr std::array<LayerDesc, 4> Layers = {{
        { "1_continentes", "continents: SeaLevel + scaled continental noise (ocean vs land, plateau height)", &TerrainManager::ElevationLayers::Continents },
        { "2_mascara_cordilheiras", "range mask: [0,1] where mountain massifs are allowed", &TerrainManager::ElevationLayers::RangeMask },
        { "3_montanhas", "mountains: eroded-FBm mountain field BEFORE the mask, evaluated everywhere", &TerrainManager::ElevationLayers::Mountains },
        { "4_final", "final: clamp(continents + hills + mountains masked by mask^1.5) - what the heightmaps store", &TerrainManager::ElevationLayers::Final },
    }};
    // One stretched + one absolute PNG per layer, plus layers_info.txt.
    constexpr u32 LayerFileCount = static_cast<u32>(Layers.size()) * 2 + 1;

    // 8-bit grayscale, `values` row-major (row = world Z, column = world X), low..high -> 0..255.
    bool WriteGrayPng(const std::filesystem::path& path, const std::vector<f32>& values, u32 resolution, f32 low, f32 high) {
        f32 range = std::max(high - low, 1e-6f);
        std::vector<u8> pixels(values.size());
        for (size_t i = 0; i < values.size(); i++) {
            pixels[i] = static_cast<u8>(std::clamp((values[i] - low) / range, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        i32 dimension = static_cast<i32>(resolution);
        return stbi_write_png(path.string().c_str(), dimension, dimension, 1, pixels.data(), dimension) != 0;
    }

    void ExportLayers() {
        const u32 res = std::max<u32>(Config.LayersResolution, 1);
        const f32 step = Config.LayersSize / static_cast<f32>(res);
        const f32 min_x = Config.LayersCenter.x - Config.LayersSize * 0.5f;
        const f32 min_z = Config.LayersCenter.y - Config.LayersSize * 0.5f;

        // A 1024^2 export is ~1M full generator evaluations - spread over every core, rows
        // interleaved. The noise objects are read-only after TerrainManager::Init().
        std::vector<TerrainManager::ElevationLayers> samples(static_cast<size_t>(res) * res);
        u32 thread_count = std::max<u32>(std::thread::hardware_concurrency(), 1);
        std::vector<std::thread> threads;
        for (u32 t = 0; t < thread_count; t++) {
            threads.emplace_back([&, t]() {
                for (u32 row = t; row < res; row += thread_count) {
                    f32 z = min_z + (static_cast<f32>(row) + 0.5f) * step;
                    for (u32 column = 0; column < res; column++) {
                        f32 x = min_x + (static_cast<f32>(column) + 0.5f) * step;
                        // Same anti-aliasing rule as a ring's WriteHeightmap: nothing finer than 2 samples.
                        samples[static_cast<size_t>(row) * res + column] = TerrainManager::SampleElevationLayers(x, z, step * 2.0f);
                    }
                }
            });
        }
        for (std::thread& thread : threads) { thread.join(); }

        std::error_code error;
        std::filesystem::create_directories(OutPath("absolute"), error);

        std::ofstream info(OutPath("layers_info.txt"));
        info << "region                x [" << min_x << ", " << min_x + Config.LayersSize << "], z [" << min_z << ", " << min_z + Config.LayersSize << "]\n"
             << "resolution            " << res << " x " << res << " (" << step << " world units per pixel)\n"
             << "image axes            +X right, +Z down (top-down view)\n"
             << "generator seed        " << TerrainManager::Generator.Seed << "\n"
             << "sea level             " << TerrainManager::SeaLevel << " normalized (" << TerrainManager::SeaLevel * GTerrainPass.Config.HeightScale << " world units)\n"
             << "height scale          " << GTerrainPass.Config.HeightScale << " world units at normalized 1.0\n"
             << "\n"
             << "<name>.png            stretched to that layer's own 0.5th..99.5th percentile over this\n"
             << "                      region (a few extreme crests would otherwise flatten the rest)\n"
             << "absolute/<name>.png   0..1 for every layer (comparable between layers)\n"
             << "hills                 not exported separately - part of 4_final\n\n";

        std::vector<f32> values(samples.size());
        for (const LayerDesc& layer : Layers) {
            f32 low = std::numeric_limits<f32>::max();
            f32 high = std::numeric_limits<f32>::lowest();
            for (size_t i = 0; i < samples.size(); i++) {
                values[i] = samples[i].*layer.Field;
                low = std::min(low, values[i]);
                high = std::max(high, values[i]);
            }
            std::vector<f32> sorted = values;
            std::sort(sorted.begin(), sorted.end());
            f32 stretch_low = sorted[sorted.size() / 200];
            f32 stretch_high = sorted[sorted.size() - 1 - sorted.size() / 200];

            std::string file_name = std::string(layer.Name) + ".png";
            if (WriteGrayPng(OutPath(file_name), values, res, stretch_low, stretch_high)) { FilesWritten++; }
            if (WriteGrayPng(OutPath("absolute") / file_name, values, res, 0.0f, 1.0f)) { FilesWritten++; }
            info << file_name << "\n    " << layer.Description
                 << "\n    range " << low << " .. " << high
                 << ", stretched " << stretch_low << " .. " << stretch_high << "\n";
        }

        size_t land = 0;
        size_t masked = 0;
        for (const TerrainManager::ElevationLayers& sample : samples) {
            if (sample.Final >= TerrainManager::SeaLevel) { land++; }
            if (sample.RangeMask > 0.0f) { masked++; }
        }
        f64 total = static_cast<f64>(samples.size());
        info << "\nland (final >= sea level)   " << 100.0 * static_cast<f64>(land) / total << " %\n"
             << "mountain mask > 0           " << 100.0 * static_cast<f64>(masked) / total << " %\n";
        if (info) { FilesWritten++; }
        analog::info("screenshots: exported generator layers ({}x{}, {} world units)", res, res, Config.LayersSize);
    }

    // ---- Lifecycle ----

    bool ParseArgs(i32& argc, char** argv) {
        i32 kept = 1;
        for (i32 i = 1; i < argc; i++) {
            std::string_view arg = argv[i];
            bool ours = arg.starts_with("--screenshots=") || arg.starts_with("--shot-");
            auto equals = arg.find('=');
            if (!ours || equals == std::string_view::npos) {
                argv[kept++] = argv[i];
                continue;
            }
            std::string_view key = arg.substr(2, equals - 2);
            std::string value(arg.substr(equals + 1));

            try {
                if (key == "screenshots") { Config.OutDir = value; }
                else if (key == "shot-set") {
                    if (value == "stages") { Config.Set = ShotSet::Stages; }
                    else if (value == "chunk") { Config.Set = ShotSet::Chunk; }
                    else if (value == "layers") { Config.Set = ShotSet::Layers; }
                    else if (value == "lod") { Config.Set = ShotSet::Lod; }
                    else {
                        analog::error("unknown shot set '{}' (expected stages, chunk, layers or lod)", value);
                        return false;
                    }
                }
                else if (key == "shot-x") { Config.X = std::stof(value); }
                else if (key == "shot-z") { Config.Z = std::stof(value); }
                else if (key == "shot-altitude") { Config.Altitude = std::stof(value); }
                else if (key == "shot-yaw") { Config.Yaw = std::stof(value); }
                else if (key == "shot-pitch") { Config.Pitch = std::stof(value); }
                else if (key == "shot-chunk") {
                    auto comma = value.find(',');
                    if (comma == std::string::npos) {
                        analog::error("--shot-chunk expects <x>,<z>, got '{}'", value);
                        return false;
                    }
                    Config.Chunk = { std::stoi(value.substr(0, comma)), std::stoi(value.substr(comma + 1)) };
                }
                else if (key == "shot-chunk-yaw") { Config.ChunkYaw = std::stof(value); }
                else if (key == "shot-chunk-pitch") { Config.ChunkPitch = std::stof(value); }
                else if (key == "shot-chunk-distance") { Config.ChunkDistance = std::stof(value); }
                else if (key == "shot-chunk-fov") { Config.ChunkFov = std::stof(value); }
                else if (key == "shot-layers-center") {
                    auto comma = value.find(',');
                    if (comma == std::string::npos) {
                        analog::error("--shot-layers-center expects <x>,<z>, got '{}'", value);
                        return false;
                    }
                    Config.LayersCenter = { std::stof(value.substr(0, comma)), std::stof(value.substr(comma + 1)) };
                }
                else if (key == "shot-layers-size") { Config.LayersSize = std::stof(value); }
                else if (key == "shot-layers-resolution") { Config.LayersResolution = static_cast<u32>(std::stoul(value)); }
                else if (key == "shot-lod-pos") {
                    auto comma = value.find(',');
                    if (comma == std::string::npos) {
                        analog::error("--shot-lod-pos expects <x>,<z>, got '{}'", value);
                        return false;
                    }
                    Config.LodPosition = { std::stof(value.substr(0, comma)), std::stof(value.substr(comma + 1)) };
                }
                else if (key == "shot-lod-altitude") { Config.LodAltitude = std::stof(value); }
                else if (key == "shot-lod-yaw") { Config.LodYaw = std::stof(value); }
                else if (key == "shot-lod-pitch") { Config.LodPitch = std::stof(value); }
                else if (key == "shot-lod-fov") { Config.LodFov = std::stof(value); }
                else if (key == "shot-lod-top-half-height") { Config.LodTopHalfHeight = std::stof(value); }
                else if (key == "shot-lod-top-sun-elevation") { Config.LodTopSunElevation = std::stof(value); }
                else if (key == "shot-lod-top-exposure") { Config.LodTopExposure = std::stof(value); }
                else if (key == "shot-chunk-size") { Config.ChunkImageSize = static_cast<u32>(std::stoul(value)); }
                else if (key == "shot-sun-azimuth") { Config.SunAzimuth = std::stof(value); }
                else if (key == "shot-sun-elevation") { Config.SunElevation = std::stof(value); }
                else if (key == "shot-width") { Config.Width = static_cast<u32>(std::stoul(value)); }
                else if (key == "shot-height") { Config.Height = static_cast<u32>(std::stoul(value)); }
                else {
                    analog::error("unrecognized argument '--{}'", key);
                    return false;
                }
            } catch (const std::exception&) {
                analog::error("malformed value in '{}'", arg);
                return false;
            }
        }
        argc = kept;
        Active = !Config.OutDir.empty();
        // All three chunk images share one square size - the window (so the renders) included.
        if (Config.Set == ShotSet::Chunk) {
            Config.Width = Config.ChunkImageSize;
            Config.Height = Config.ChunkImageSize;
        }
        return true;
    }

    void Init(Camera& camera) {
        // CPU-only: no frame is ever rendered - Game::Run() sees the close request immediately.
        if (Config.Set == ShotSet::Layers) {
            std::error_code error;
            std::filesystem::create_directories(Config.OutDir, error);
            ExpectedFiles = LayerFileCount;
            ExportLayers();
            CurrentPhase = Phase::Done;
            Platform::RequestClose();
            return;
        }

        ShotCamera = &camera;
        DefaultNearPlane = camera.NearPlane;
        DefaultFarPlane = camera.FarPlane;

        if (Config.Set == ShotSet::Chunk) {
            Shots = ChunkShots;
            ExpectedFiles = static_cast<u32>(ChunkShots.size()) + 1;   // + 1_heightmap.png
        } else if (Config.Set == ShotSet::Lod) {
            Shots = LodShots;
            ExpectedFiles = static_cast<u32>(LodShots.size());
            for (u32 i = 0; i < GTerrainPass.RingColors.size(); i++) {
                analog::info("screenshots: ring {} color #{:06X}", i, GTerrainPass.RingColors[i]);
            }
        } else {
            Shots = StageShots;
            ExpectedFiles = static_cast<u32>(StageShots.size());
        }

        std::error_code error;
        std::filesystem::create_directories(Config.OutDir, error);
        if (error) {
            analog::error("couldn't create screenshot directory '{}': {}", Config.OutDir, error.message());
        }
        // A shot counts as written if its file exists after the capture, so a previous run's
        // file must not be there to count instead.
        for (const Shot& shot : Shots) {
            std::filesystem::remove(ShotPath(shot), error);
        }

        // Nothing but the scene in the frame: no perf overlay, no ImGui toolbar (every debug
        // window already starts closed), no watermark/crosshair (Game::Run() skips those).
        PerfOverlay::Visible = false;
        DebugPanel::ToolbarVisible = false;
        GSkyPass.AnimateSun = false;
        GSkyPass.SunAzimuthDegrees = Config.SunAzimuth;
        GSkyPass.SunElevationDegrees = Config.SunElevation;

        if (Config.Set == ShotSet::Chunk) {
            // Orbit the chunk's center. The camera stays well inside ring 0's radius of the chunk,
            // so the chunk is always resident at ring 0's resolution.
            vec3 target = ChunkCenter();
            camera.Fov = Config.ChunkFov;
            camera.Front = FrontFromAngles(Config.ChunkYaw, Config.ChunkPitch);
            camera.Position = target - camera.Front * Config.ChunkDistance;
            if (camera.Position.y < TerrainManager::SampleSurfaceHeight(camera.Position.x, camera.Position.z)) {
                analog::warn("screenshots: camera is below the terrain - raise --shot-chunk-pitch or --shot-chunk-distance");
            }
        } else if (Config.Set == ShotSet::Lod) {
            // Streams the world from the wireframe view's pose; the top-down view shares its XZ.
            PlaceLodCamera(camera, ShotView::LodPlayer);
        } else {
            camera.Position = vec3(
                Config.X,
                TerrainManager::SampleSurfaceHeight(Config.X, Config.Z) + Config.Altitude,
                Config.Z
            );
            camera.Front = FrontFromAngles(Config.Yaw, Config.Pitch);
        }
        UpdateMatrices(camera);

        analog::info(
            "screenshots: camera at ({:.1f}, {:.1f}, {:.1f}) -> '{}'",
            camera.Position.x, camera.Position.y, camera.Position.z, Config.OutDir
        );

        CurrentPhase = Phase::WaitWorld;
        PhaseTime = 0.0f;
    }

    void Update(f32 delta_time) {
        PhaseTime += delta_time;

        switch (CurrentPhase) {
            case Phase::WaitWorld: {
                CompleteFrames = TerrainManager::IsWorldComplete() ? CompleteFrames + 1 : 0;
                bool timed_out = PhaseTime > WorldWaitTimeout;
                if (CompleteFrames >= CompleteFramesRequired || timed_out) {
                    if (timed_out) {
                        analog::warn("screenshots: world still incomplete after {:.0f} s - capturing anyway", WorldWaitTimeout);
                    } else {
                        analog::info("screenshots: world complete after {:.1f} s", PhaseTime);
                    }
                    if (Config.Set == ShotSet::Chunk) {
                        ExportHeightmap();
                    }
                    CurrentPhase = Phase::Settle;
                    PhaseTime = 0.0f;
                }
                break;
            }
            case Phase::Settle:
                if (PhaseTime >= FirstShotSettle) {
                    ShotIndex = 0;
                    ApplyShot(Shots[0]);
                    CurrentPhase = Phase::Capturing;
                }
                break;
            case Phase::Capturing: {
                const Shot& shot = Shots[ShotIndex];
                if (!Requested) {
                    if (++ShotFrames < ShotSettleFrames) { break; }
                    Requested = GRenderer.RequestCapture(ShotPath(shot).string());
                    if (shot.View != ShotView::Fixed) {
                        const TerrainManager::Stats& ring0 = TerrainManager::Ring0.DebugStats;
                        analog::info("screenshots: '{}' ring 0 drawn {} culled {}", shot.Name, ring0.DrawnLastFrame, ring0.CulledLastFrame);
                        for (u32 i = 0; i < TerrainManager::OuterRingCount; i++) {
                            const TerrainManager::Stats& ring = TerrainManager::OuterRings[i].DebugStats;
                            analog::info("screenshots: '{}' ring {} drawn {} culled {}", shot.Name, i + 1, ring.DrawnLastFrame, ring.CulledLastFrame);
                        }
                    }
                    if (!Requested) {
                        CurrentPhase = Phase::Done;
                        Platform::RequestClose();
                    }
                    break;
                }
                if (GRenderer.CapturePending) { break; }

                if (std::filesystem::exists(ShotPath(shot))) { FilesWritten++; }
                if (Config.Set == ShotSet::Chunk && (GRenderer.Swapchain.Width != Config.ChunkImageSize || GRenderer.Swapchain.Height != Config.ChunkImageSize)) {
                    analog::warn(
                        "screenshots: '{}' is {}x{}, not {}x{} - the window didn't get its requested size (screen too small or DPI scaling?)",
                        shot.Name, GRenderer.Swapchain.Width, GRenderer.Swapchain.Height, Config.ChunkImageSize, Config.ChunkImageSize
                    );
                }
                if (++ShotIndex < Shots.size()) {
                    ApplyShot(Shots[ShotIndex]);
                } else {
                    TerrainManager::IsolateChunk = false;
                    analog::info("screenshots: {} of {} files written to '{}'", FilesWritten, ExpectedFiles, Config.OutDir);
                    CurrentPhase = Phase::Done;
                    Platform::RequestClose();
                }
                break;
            }
            case Phase::Done:
                break;
        }
    }

    bool Succeeded() {
        return ExpectedFiles > 0 && FilesWritten == ExpectedFiles;
    }
}
