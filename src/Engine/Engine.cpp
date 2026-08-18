#include "Engine.hpp"

#include <imgui.h>

#include "Renderer/Renderer.hpp"
#include "Engine/Core/FileIO.hpp"
#include "Engine/Core/Window.hpp"
#include "Engine/Core/Platform.hpp"
#include "Engine/Core/FrameTimer.hpp"
#include "Engine/Core/TaskScheduler/TaskScheduler.hpp"

namespace Engine {
    FrameTimer Timer(Config.TargetFps);

    void ResizeEvent(i32 width, i32 height);

    IncResult Create(Camera& Camera) {
        // Core
        INC_CHECK(
            Platform::Initialize(
                static_cast<i32>(Config.Width),
                static_cast<i32>(Config.Height),
                Config.ApplicationTitle.data(),
                Engine::ResizeEvent
            ),
            "couldn't create platform layer."
        );

        FileIO::Initialize();

        TaskScheduler::Create();

        // Renderer
        INC_CHECK(
            Renderer::Create(),
            "couldn't create renderer."
        );

        Renderer::BindCamera(&Camera);

        return IncResult::SUCCESS;
    }

    void Destroy() {
        Renderer::Destroy();
        Platform::Shutdown();
        TaskScheduler::Destroy();
    }

    FrameInfo Frame() {
        FrameInfo frame;
        frame.DeltaTime = Timer.Tick();

        // Actual frame starts
        Platform::Update();
        Renderer::Frame();
        // Actual frame ends

        Timer.Sleep();

        return frame;
    }

    void ResizeEvent(i32 width, i32 height) {
        Config.Width = static_cast<u32>(width);
        Config.Height = static_cast<u32>(height);
        Renderer::Resize(width, height);
    }

    bool ShouldClose() {
        return Platform::ShouldClose();
    }

    void RefreshConfig() {
        Window::SetTitle(Config.ApplicationTitle.data());
        Window::SetSize(Config.Width, Config.Height);
        Timer.SetTargetFPS(static_cast<f32>(Config.TargetFps));
        Renderer::CurrentCamera->Fov = static_cast<f32>(Config.FOV);
    }
}
