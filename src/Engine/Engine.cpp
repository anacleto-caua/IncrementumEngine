#include "Engine.hpp"

#include "Engine/Core/Input.hpp"
#include "Engine/Core/Platform.hpp"
#include "TaskScheduler/TaskScheduler.hpp"

namespace Engine {
    void ResizeEvent(u32 width, u32 height);

    IncResult Create() {
        INC_CHECK(Platform::Initialize(1280, 720, "yes", Engine::ResizeEvent), IncResult::FAIL , "Couldn't create platform layer.");
        TaskScheduler::Create();

        Input::Mouse::RegisterCallback(Input::Mouse::Button::Left, []() {
            analog::info("mouse pos - x: {} - y: {}", Input::Mouse::XPos, Input::Mouse::YPos);
        });

        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Space, []() {
            analog::info("space was pressed");
        });

        return IncResult::SUCCESS;
    }

    void Run() {
        while(!Platform::ShouldClose()) {
            Platform::Update();
            if(Input::Keyboard::IsKeyDown(Input::Keyboard::Key::Interact)) {
                analog::info("interact key is down");
            }

            if(Input::Mouse::IsButtonDown(Input::Mouse::Button::Right)) {
                analog::info("mouse delta - x: {} - y: {}", Input::Mouse::XDelta, Input::Mouse::YDelta);
            }
        }
    }

    void Destroy() {
        Platform::Shutdown();
        TaskScheduler::Destroy();
    }

    void ResizeEvent(u32 width, u32 height) {
        analog::info("resize event - w: {} - h: {}", width, height);
    }
}
