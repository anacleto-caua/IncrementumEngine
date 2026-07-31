#include "Engine.hpp"

#include <imgui.h>

#include "Renderer/Renderer.hpp"
#include "Engine/Core/FileIO.hpp"
#include "Engine/Core/Platform.hpp"
#include "Engine/Core/FrameTimer.hpp"
#include "Engine/Core/TaskScheduler/TaskScheduler.hpp"

namespace Engine {
    FrameTimer Timer(Config.TargetFps);

    void ResizeEvent(i32 width, i32 height);

    IncResult Create(Camera3D& Camera) {
        INC_CHECK(
            Platform::Initialize(
                static_cast<i32>(Config.Width),
                static_cast<i32>(Config.Height),
                Config.EngineName.data(),
                Engine::ResizeEvent
            ),
            "couldn't create platform layer."
        );

        FileIO::Initialize();

        INC_CHECK(
            Renderer::Create(),
            "couldn't create renderer."
        );

        Renderer::BindCamera(&Camera);

        TaskScheduler::Create();

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
        Renderer::Resize(width, height);
    }

    bool ShouldClose() {
        return Platform::ShouldClose();
    }
}
