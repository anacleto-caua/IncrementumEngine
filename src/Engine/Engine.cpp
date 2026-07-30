#include "Engine.hpp"

#include <imgui.h>

#include "Renderer/Renderer.hpp"
#include "Engine/Core/FileIO.hpp"
#include "Engine/Core/Platform.hpp"
#include "Engine/Core/FrameTimer.hpp"
#include "Engine/Utils/ImGuiUtils.hpp"
#include "TaskScheduler/TaskScheduler.hpp"

namespace Engine {
    constexpr std::string_view ENGINE_NAME = "Incrementum Engine";
    constexpr u32 WIDTH = 1280;
    constexpr u32 HEIGHT = 720;
    constexpr u32 TARGET_FPS = 165;
    constexpr u32 FOV = 110;

    static constexpr std::chrono::duration<double> TARGET_FRAME_TIME{1.0 / TARGET_FPS};


    Camera3D MainCamera;

    void ResizeEvent(i32 width, i32 height);

    IncResult Create() {
        INC_CHECK(
            Platform::Initialize(WIDTH, HEIGHT, ENGINE_NAME.data(), Engine::ResizeEvent),
            "couldn't create platform layer."
        );

        FileIO::Initialize();

        INC_CHECK(
            Renderer::Create(),
            "couldn't create renderer."
        );

        // Seem's hacky
        MainCamera = CreateCamera3D(static_cast<f32>(WIDTH)/static_cast<f32>(HEIGHT), FOV);
        MainCamera.Position = { 0, 5, 0 };
        RefreshMVP(MainCamera);
        Renderer::BindCamera(&MainCamera);
        FlyBy::Create(MainCamera);

        TaskScheduler::Create();

        return IncResult::SUCCESS;
    }

    void Run() {
        FrameTimer timer(TARGET_FPS);
        while(!Platform::ShouldClose()) {
            f32 delta_time = timer.Tick();

            // Actual frame starts
            ImGuiUtils::OutFps(delta_time);

            Platform::Update();
            Renderer::Frame();

            FlyBy::Update(delta_time);
            // Actual frame ends

            timer.Sleep();
        }
    }

    void Destroy() {
        Renderer::Destroy();
        Platform::Shutdown();
        TaskScheduler::Destroy();
    }

    void ResizeEvent(i32 width, i32 height) {
        Renderer::Resize(width, height);
    }
}
