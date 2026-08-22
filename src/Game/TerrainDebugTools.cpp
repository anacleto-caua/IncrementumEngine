#include "TerrainDebugTools.hpp"

#include <cmath>
#include <fstream>
#include <chrono>
#include <ctime>

#include <stb/stb_easy_font.h>

#include "Game/Game.hpp"
#include "Renderer/Camera.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/InputContext.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"

namespace TerrainDebugTools {
    constexpr const char* LogFilePath = "terrain_debug_log.txt";
    constexpr f32 StateLogIntervalSeconds = 0.5f;

    enum class Action {
        MarkDarkChunk,
        _COUNT_
    };
    InputContext<Action, Action::_COUNT_> Context;

    Camera* TrackedCamera = nullptr;
    f32 TimeSinceLastStateLog = 0.0f;

    std::string Timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm {};
#if defined(_WIN32)
        localtime_s(&local_tm, &now_time);
#else
        localtime_r(&now_time, &local_tm);
#endif
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local_tm);
        return std::string(buffer);
    }

    // One line per ring, every StateLogIntervalSeconds - a timeline of the same counters the
    // "Terrain Draw Data" ImGui panel shows, so a persistent-vs-transient dark chunk shows up as a
    // sustained non-zero VisibleMissingLastFrame run rather than a single sample.
    template<typename RingT>
    void LogRingState(std::ofstream& out, const char* name, RingT& ring) {
        out << "  " << name
            << " Drawn=" << ring.DebugStats.DrawnLastFrame
            << " Culled=" << ring.DebugStats.CulledLastFrame
            << " Dark=" << ring.DebugStats.VisibleMissingLastFrame
            << " GenInFlight=" << ring.DebugStats.GenerationsInFlight
            << " Generated=" << ring.DebugStats.ChunksGenerated
            << " Evictions=" << ring.DebugStats.Evictions
            << "\n";
    }

    void LogPeriodicState() {
        std::ofstream out(LogFilePath, std::ios::app);
        if (!out.is_open()) { return; }

        out << "[" << Timestamp() << "] tick=" << TerrainManager::CurrentTick;
        if (TrackedCamera) {
            out << " pos=(" << TrackedCamera->Position.x << "," << TrackedCamera->Position.y << "," << TrackedCamera->Position.z << ")"
                << " look=(" << TrackedCamera->Front.x << "," << TrackedCamera->Front.y << "," << TrackedCamera->Front.z << ")";
        }
        out << "\n";

        LogRingState(out, "Ring0", TerrainManager::Ring0);
        for (u32 i = 0; i < TerrainManager::OuterRingCount; i++) {
            char label[16];
            std::snprintf(label, sizeof(label), "Ring%u", i + 1);
            LogRingState(out, label, TerrainManager::OuterRings[i]);
        }
    }

    void LogOneChunkDiagnostic(std::ofstream& out, i32 ring_index, ivec2 chunk_pos, f32 ray_distance, const Frustum& frustum) {
        TerrainManager::ChunkDiagnostic diag = TerrainManager::DiagnoseChunk(ring_index, chunk_pos, frustum);

        out << "  ring=" << ring_index
            << " chunk=(" << chunk_pos.x << "," << chunk_pos.y << ")"
            << " ray_dist=" << ray_distance
            << " | in_cache_map=" << diag.InCacheMap
            << " cache_valid=" << diag.CacheValid
            << " last_used_tick=" << diag.LastUsedTick
            << " (now=" << TerrainManager::CurrentTick << ")"
            << " | in_gen_pool=" << diag.InGenerationPool
            << " gen_done=" << diag.GenerationDone
            << " | global_layer=" << diag.GlobalLayer
            << " in_draw_list=" << diag.InDrawListThisFrame
            << " intersects_frustum_now=" << diag.IntersectsFrustum
            << "\n";
    }

    // Ray-marches from the camera along its look direction, logging every distinct (ring, chunk)
    // it crosses - not just the first one - so the log shows the whole chain from the player out
    // to whatever they're looking at, not a single guessed sample point. Steps in XZ only (ignores
    // the ray's real intersection with terrain height) since chunk identity only depends on XZ -
    // good enough for "which chunk column is under the crosshair", which is all this needs.
    void MarkDarkChunk() {
        if (!TrackedCamera) { return; }

        std::ofstream out(LogFilePath, std::ios::app);
        if (!out.is_open()) {
            analog::warn("TerrainDebugTools: failed to open {}", LogFilePath);
            return;
        }

        vec3 origin = TrackedCamera->Position;
        vec3 dir = math::normalize(TrackedCamera->Front);
        const Frustum& frustum = TrackedCamera->Frustum;

        out << "==== MARK [" << Timestamp() << "] "
            << "player_pos=(" << origin.x << "," << origin.y << "," << origin.z << ") "
            << "look_dir=(" << dir.x << "," << dir.y << "," << dir.z << ") ====\n";

        constexpr f32 StepSize = 20.0f;
        constexpr f32 MaxDistance = 20000.0f;

        ivec2 last_chunk = { INT32_MIN, INT32_MIN };
        i32 last_ring = -2;
        u32 chunks_logged = 0;

        for (f32 dist = StepSize; dist <= MaxDistance; dist += StepSize) {
            vec3 sample = origin + dir * dist;
            // Ring ownership in TerrainManager is centered on the player's current position, not
            // world origin - RefreshRing's InnerRadius/OuterRadius circles are built from
            // player_coord, so this must measure from the player too, not (0,0,0).
            f64 rel_x = static_cast<f64>(sample.x) - static_cast<f64>(origin.x);
            f64 rel_z = static_cast<f64>(sample.z) - static_cast<f64>(origin.z);
            f64 horiz_dist = std::sqrt(rel_x * rel_x + rel_z * rel_z);

            f64 chunk_scale = 0.0;
            i32 ring_index = TerrainManager::FindRingForDistance(horiz_dist, chunk_scale);
            if (ring_index < 0) { break; } // past all streamed coverage - ray only gets farther from here

            ivec2 chunk_pos = {
                static_cast<i32>(std::floor(static_cast<f64>(sample.x) / chunk_scale)),
                static_cast<i32>(std::floor(static_cast<f64>(sample.z) / chunk_scale))
            };

            if (chunk_pos.x == last_chunk.x && chunk_pos.y == last_chunk.y && ring_index == last_ring) {
                continue; // same chunk as the previous sample - only log on a transition
            }
            last_chunk = chunk_pos;
            last_ring = ring_index;

            LogOneChunkDiagnostic(out, ring_index, chunk_pos, dist, frustum);
            chunks_logged++;
        }

        out << "==== END MARK (" << chunks_logged << " chunks along ray) ====\n\n";
        analog::info("TerrainDebugTools: marked, {} chunks logged to {}", chunks_logged, LogFilePath);
    }

    void Init(Camera& camera) {
        TrackedCamera = &camera;

        Context.BindKey(Action::MarkDarkChunk, Input::Key::F6);
        Context.OnPressed(Action::MarkDarkChunk, []() { MarkDarkChunk(); });

        std::ofstream out(LogFilePath, std::ios::app);
        if (out.is_open()) {
            out << "\n==== SESSION START [" << Timestamp() << "] ====\n";
            out << "Press F6 in-game to mark the chunk you're looking at as dark.\n";
        }

        analog::info("TerrainDebugTools: logging to {} - press F6 to mark a dark chunk", LogFilePath);
    }

    // MarkDarkChunk() ray-marches along TrackedCamera->Front; this gives the player an on-screen
    // reference for where that actually points. Reuses TextPass rather than a new draw path.
    void DrawCrosshair() {
        constexpr f32 Scale = 3.0f;
        char glyph[] = "+";
        f32 half_width = static_cast<f32>(stb_easy_font_width(glyph)) * Scale * 0.5f;
        f32 half_height = static_cast<f32>(stb_easy_font_height(glyph)) * Scale * 0.5f;

        f32 center_x = static_cast<f32>(GRenderer.Swapchain.Width) * 0.5f;
        f32 center_y = static_cast<f32>(GRenderer.Swapchain.Height) * 0.5f;

        GTextPass.DrawText("+", center_x - half_width, center_y - half_height, vec3(1.0f, 1.0f, 1.0f), Scale);
    }

    void Update(f32 delta_time) {
        Context.Frame();
        DrawCrosshair();

        TimeSinceLastStateLog += delta_time;
        if (TimeSinceLastStateLog >= StateLogIntervalSeconds) {
            TimeSinceLastStateLog = 0.0f;
            LogPeriodicState();
        }
    }
}
