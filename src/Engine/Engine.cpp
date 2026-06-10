#include "Engine.hpp"

#include <imgui.h>

#include "Renderer/Renderer.hpp"
#include "Engine/Core/Platform.hpp"
#include "TaskScheduler/TaskScheduler.hpp"

namespace Engine {
    Camera3D MainCamera;

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

        // Seem's hacky
        MainCamera = CreateCamera3D(static_cast<f32>(1280)/static_cast<f32>(720), 90);
        Renderer::BindCamera(&MainCamera);
        FlyBy::Create(MainCamera);

        TaskScheduler::Create();

        return IncResult::SUCCESS;
    }

    void Run() {
        while(!Platform::ShouldClose()) {
            bool show_demo_window = true;
            ImGui::ShowDemoWindow(&show_demo_window);

            Platform::Update();
            Renderer::Frame();

            // TODO: Add delta time
            FlyBy::Update(1);
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
