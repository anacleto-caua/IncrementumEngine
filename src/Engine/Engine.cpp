#include "Engine.hpp"

#include "Renderer/Renderer.hpp"
#include "Engine/Core/Platform.hpp"
#include "TaskScheduler/TaskScheduler.hpp"

namespace Engine {
    void ResizeEvent(i32 width, i32 height);

    IncResult Create() {
        INC_CHECK(
            Platform::Initialize(1280, 720, "yes", Engine::ResizeEvent),
            "Couldn't create platform layer."
        );

        INC_CHECK(
            Renderer::Create(),
            "Couldn't create renderer."
        );

        TaskScheduler::Create();

        return IncResult::SUCCESS;
    }

    void Run() {
        while(!Platform::ShouldClose()) {
            Platform::Update();
            Renderer::Frame();
        }
    }

    void Destroy() {
        Renderer::Destroy();
        Platform::Shutdown();
        TaskScheduler::Destroy();
    }

    void ResizeEvent(i32 width, i32 height) {
        analog::info("resize event - w: {} - h: {}", width, height);
    }
}
