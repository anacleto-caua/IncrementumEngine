#include "Game.hpp"

#include <imgui.h>

#include "Renderer/Camera.hpp"
#include "Game/FlyByCamera.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/InputContext.hpp"
#include "Engine/Core/Window.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"

namespace Game {
    void OutFps(f32 delta_time);

    enum class Action {
        ToggleFullscreen,
        ReleaseMouse,
        CaptureMouse,

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
        UpdateMatrices(MainCamera);

        INC_CHECK(Engine.Init(MainCamera), "couldn't create engine");
        Engine.RefreshConfig();

        TerrainManager::Init();

        FlyByCamera::Bind(MainCamera);

        // Good taste stuff
        Context.BindKey(Action::ToggleFullscreen, Input::Key::F11);
        Context.BindKey(Action::ReleaseMouse, Input::Key::Escape);
        Context.BindButton(Action::CaptureMouse, Input::MouseButton::Left);

        Context.OnPressed(Action::ToggleFullscreen, [](){ Window::ToggleFullscreen(); });
        Context.OnPressed(Action::ReleaseMouse, []() { Input::FreeMouse(); });
        Context.OnPressed(Action::CaptureMouse, []() { Input::CaptureMouse(); });

        return IncResult::SUCCESS;

    }

    void Run() {
        while (!Engine.ShouldClose()) {
            FrameInfo frame = Engine.Frame();

            // TODO: Idk if I should feed the camera here, maybe I should just get the renderer camera from inside this system, hum
            TerrainManager::RefreshChunks(MainCamera.Position, MainCamera.Frustum);

            Context.Frame();

            OutFps(frame.DeltaTime);
            FlyByCamera::Update(frame.DeltaTime);
        }
    }

    void Destroy() {
        Engine.Destroy();
    }

    void OutFps(f32 delta_time) {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

        // Position: Bottom-Right
        const f32 pad = 10.0f;
        const ImVec2 viewport_pos = ImGui::GetMainViewport()->WorkPos;
        const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
        ImVec2 window_pos = { (viewport_pos.x + viewport_size.x - pad), (viewport_pos.y + viewport_size.y - pad) };
        ImVec2 window_pos_pivot = { 1.0f, 1.0f }; // Pivot on bottom-right corner

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

        if (ImGui::Begin("Perf Overlay", nullptr, window_flags)) {
            ImGui::Text("FPS: %.1f (%.3f ms)", 1.0f / delta_time, delta_time * 1000.0f);
        }
        ImGui::End();
    }

}
