#include "Engine.hpp"

#include "Core/Window.hpp"
#include "TaskScheduler/TaskScheduler.hpp"

namespace Engine {

    void ResizeEvent(u32 width, u32 height);

    IncResult Create() {
        INC_CHECK(Window::Create(1280, 860, "yes", Engine::ResizeEvent), IncResult::FAIL , "Couldn't create window.");
        TaskScheduler::Create();
        return IncResult::SUCCESS;
    }

    void Run() {
        while(!Window::ShouldClose()) {
            Window::Update();
        }
    }

    void Destroy() {
        Window::Destroy();
        TaskScheduler::Destroy();
    }

    void ResizeEvent(u32 width, u32 height) {
        analog::info("resize event - w: {} - h: {}", width, height);
    }
}
