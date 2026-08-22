#include "Engine.hpp"

#include <imgui.h>

#include "Renderer/Camera.hpp"
#include "Engine/Core/FileIO.hpp"
#include "Engine/Core/Window.hpp"
#include "Engine/Core/Platform.hpp"
#include "Engine/Core/TaskScheduler/TaskScheduler.hpp"

IncResult Engine::Init(Camera& camera) {
    // Core
    INC_CHECK(
        Platform::Initialize(
            static_cast<i32>(Config.Width),
            static_cast<i32>(Config.Height),
            Config.ApplicationTitle.data(),
            [this](i32 width, i32 height) { ResizeEvent(width, height); }
        ),
        "couldn't create platform layer."
    );

    FileIO::Initialize();

    TaskScheduler::Create();

    // Renderer
    INC_CHECK(
        Renderer.Init(),
        "couldn't create renderer."
    );

    Renderer.BindCamera(&camera);

    return IncResult::SUCCESS;
}

void Engine::Destroy() {
    Renderer.Destroy();
    Platform::Shutdown();
    TaskScheduler::Destroy();
}

void Engine::Frame() {
    // Paced before Tick(), not after the frame's work: Sleep() spins against the *previous*
    // Tick()'s start point, so waiting here covers the whole previous loop iteration - including
    // the game-side work that runs outside this function (TerrainManager::RefreshChunks() before
    // it, FlyByCamera::Update() and the ImGui overlay after it). Sleeping at the end of this
    // function instead would only pace Platform::Update() + Renderer.Frame(), leaving that game
    // work as unpaced time added on top of the target and making the loop consistently overshoot.
    Timer.Sleep();

    CurrentFrame.DeltaTime = Timer.Tick();

    Platform::Update();
    Renderer.Frame();
}

void Engine::ResizeEvent(i32 width, i32 height) {
    Config.Width = static_cast<u32>(width);
    Config.Height = static_cast<u32>(height);
    Renderer.Resize(width, height);
}

bool Engine::ShouldClose() {
    return Platform::ShouldClose();
}

void Engine::RefreshConfig() {
    Window::SetTitle(Config.ApplicationTitle.data());
    Window::SetSize(Config.Width, Config.Height);
    Timer.SetTargetFPS(static_cast<f32>(Config.TargetFps));
    Renderer.CurrentCamera->Fov = static_cast<f32>(Config.FOV);
}
