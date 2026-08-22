#include "Game.hpp"

#include "Renderer/Camera.hpp"
#include "Game/FlyByCamera.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/InputContext.hpp"
#include "Engine/Core/PerfOverlay.hpp"
#include "Engine/Core/Window.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"

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

    IncResult Create() {
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

        INC_CHECK(Engine.Init(MainCamera), "couldn't create engine");
        Engine.RefreshConfig();

        TerrainManager::Init();

        FlyByCamera::Bind(MainCamera);

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

            // TODO: Idk if I should feed the camera here, maybe I should just get the renderer camera from inside this system, hum
            TerrainManager::RefreshChunks(MainCamera.Position, MainCamera.Frustum);

            Context.Frame();

            PerfOverlay::Frame(Engine.CurrentFrame.DeltaTime);
            FlyByCamera::Update(Engine.CurrentFrame.DeltaTime);

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
    }

    void Destroy() {
        Engine.Destroy();
    }
}
