#include "Engine.hpp"

#include <chrono>

#include <imgui.h>

#include "Renderer/Renderer.hpp"
#include "Engine/Core/FileIO.hpp"
#include "Engine/Core/Platform.hpp"
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
        MainCamera.Position = { -10, 15, -10 };
        RefreshMVP(MainCamera);
        Renderer::BindCamera(&MainCamera);
        FlyBy::Create(MainCamera);

        TaskScheduler::Create();

        return IncResult::SUCCESS;
    }

    void Run() {
        auto last_frame_time = std::chrono::high_resolution_clock::now();
        while(!Platform::ShouldClose()) {
            auto frame_begin = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> raw_delta_time = frame_begin - last_frame_time;
            float delta_time = raw_delta_time.count();
            last_frame_time = frame_begin;

            // Actual frame starts
            ImGuiUtils::OutFps(delta_time);

            Platform::Update();
            Renderer::Frame();

            FlyBy::Update(delta_time);
            // Actual frame ends

            auto frame_end = std::chrono::high_resolution_clock::now();
            auto frame_elapsed_time = frame_end - frame_begin;

            if (frame_elapsed_time < TARGET_FRAME_TIME) {
                auto time_to_sleep = TARGET_FRAME_TIME - frame_elapsed_time;

                if (time_to_sleep > std::chrono::milliseconds(2)) {
                    std::this_thread::sleep_for(time_to_sleep - std::chrono::milliseconds(1));
                }

                // Spin-lock busy wait
                while (std::chrono::high_resolution_clock::now() - frame_begin < TARGET_FRAME_TIME) {
                    std::this_thread::yield();
                }
            }
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
