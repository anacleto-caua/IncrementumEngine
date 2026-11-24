#include "Game.hpp"

#include "Renderer/Camera.hpp"
#include "Game/FlyByCamera.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/InputContext.hpp"
#include "Engine/Core/PerfOverlay.hpp"
#include "Engine/Core/Window.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"
#include "Game/TerrainDebugTools.hpp"
#include "Game/Benchmark/Benchmark.hpp"
#include "Game/Screenshots/Screenshots.hpp"
#include "Renderer/VkVault.hpp"

#include <chrono>

namespace Game {
    enum class Action {
        ToggleFullscreen,
        ReleaseMouse,
        CaptureMouse,
        TogglePerfOverlay,

        _COUNT_
    };

    InputContext<Action, Action::_COUNT_> Context;

    Camera MainCamera = {
        .Position = vec3::ZERO,
        .Front = vec3::FORWARD,
        .Up = vec3::UP,
        .Fov = 90.0f,
        .AspectRatio = 16.0f / 9.0f,
        .NearPlane = 0.1f,
        .FarPlane = 1000.0f,
        .View = mat4(1.0f),
        .Projection = mat4(1.0f)
    };

    IncResult Create(const Benchmark::BenchConfig& bench_config) {
        // Benchmark runs: fixed resolution, no frame cap, IMMEDIATE present when available, and an
        // explicit worker count - all of which must be decided before Engine.Init() creates them.
        if (Benchmark::Active) {
            GEngine.Config.Width = bench_config.Width;
            GEngine.Config.Height = bench_config.Height;
            GEngine.Config.Uncapped = true;
            GEngine.Config.Workers = bench_config.Workers;
            VkVault::PreferImmediatePresent = true;
        }
        if (Screenshots::Active) {
            GEngine.Config.Width = Screenshots::Config.Width;
            GEngine.Config.Height = Screenshots::Config.Height;
        }

        MainCamera =
            CreateCamera(
                static_cast<f32>(GEngine.Config.Width) / static_cast<f32>(GEngine.Config.Height),
                static_cast<f32>(GEngine.Config.FOV)
            );

        MainCamera.Position = { 0, 75, 0 };
        // Must reach at least TerrainManager::TotalCoverageRadius or the LOD ring system's outer
        // rings get clipped/frustum-culled away regardless of streaming - see the comment on
        // TotalCoverageRadius. The outermost ring's own ChunkScale as margin covers a chunk AABB's
        // far corner extending slightly past the nominal radius.
        MainCamera.FarPlane = static_cast<f32>(TerrainManager::TotalCoverageRadius + TerrainManager::OuterRingChunkScales.back());
        UpdateMatrices(MainCamera);

        auto engine_init_start = std::chrono::steady_clock::now();
        INC_CHECK(Engine.Init(MainCamera), "couldn't create engine");
        f32 engine_init_ms = std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - engine_init_start).count();
        Engine.RefreshConfig();

        TerrainManager::Init();

        // Spawn a fixed distance above whatever the generator put at the origin, rather than at a
        // hardcoded height that could land underwater or inside a mountain.
        MainCamera.Position.y = TerrainManager::SampleSurfaceHeight(MainCamera.Position.x, MainCamera.Position.z) + 60.0f;
        UpdateMatrices(MainCamera);

        FlyByCamera::Bind(MainCamera);
        TerrainDebugTools::Init(MainCamera);

        if (Benchmark::Active) {
            Benchmark::Init(bench_config, MainCamera, engine_init_ms);
        }
        if (Screenshots::Active) {
            Screenshots::Init(MainCamera);
        }

        // Good taste stuff
        Context.BindKey(Action::ToggleFullscreen, Input::Key::F11);
        Context.BindKey(Action::ReleaseMouse, Input::Key::Escape);
        Context.BindButton(Action::CaptureMouse, Input::MouseButton::Left);
        Context.BindKey(Action::TogglePerfOverlay, Input::Key::F3);

        Context.OnPressed(Action::ToggleFullscreen, [](){ Window::ToggleFullscreen(); });
        Context.OnPressed(Action::ReleaseMouse, []() { Input::FreeMouse(); });
        Context.OnPressed(Action::CaptureMouse, []() { Input::CaptureMouse(); });
        Context.OnPressed(Action::TogglePerfOverlay, []() { PerfOverlay::Visible = !PerfOverlay::Visible; });

        return IncResult::SUCCESS;

    }

    void Run() {
        while (!Engine.ShouldClose()) {
            Engine.Frame();

            // Must run before RefreshChunks(): it rotates MainCamera and recomputes its Frustum for
            // this frame. Running RefreshChunks() first culls against last frame's frustum, which
            // shows up as a black leading edge during a fast turn.
            // The benchmark's scripted camera replaces the fly-by one, same ordering constraint.
            if (Benchmark::Active) {
                Benchmark::BeginFrame(Engine.CurrentFrame.DeltaTime);
            } else if (Screenshots::Active) {
                Screenshots::Update(Engine.CurrentFrame.DeltaTime);
            } else {
                FlyByCamera::Update(Engine.CurrentFrame.DeltaTime);
            }

            // TODO: Idk if I should feed the camera here, maybe I should just get the renderer camera from inside this system, hum
            TerrainManager::RefreshChunks(MainCamera.Position, MainCamera.Frustum);

            Context.Frame();

            PerfOverlay::Frame(Engine.CurrentFrame.DeltaTime);
            // Its periodic log-file dump would be file I/O inside the measured window, and its
            // crosshair would end up in every screenshot.
            if (!Benchmark::Active && !Screenshots::Active) {
                TerrainDebugTools::Update(Engine.CurrentFrame.DeltaTime);
            }

            if (!Screenshots::Active) {
                // Simplest possible exercise of TextPass: a static bottom-left watermark. Real
                // HUD/label usage can call GTextPass.DrawText() from anywhere. Reads the window's own
                // framebuffer size rather than GRenderer.Swapchain (Renderer's internal, GPU-precise
                // extent, kept for TextPass's own NDC math) - a UI label's position doesn't need that
                // precision, and Game code has no reason to reach into Renderer's swapchain state for it.
                i32 window_width, window_height;
                Window::GetFramebufferSize(window_width, window_height);

                GTextPass.DrawText(
                    "Incrementum Engine",
                    10.0f,
                    static_cast<f32>(window_height) - 25.0f,
                    vec3(0.85f, 0.0f, 0.85f),
                    2.5f
                );
            }

            Benchmark::EndFrame();
        }
    }

    void Destroy() {
        Engine.Destroy();
    }
}
