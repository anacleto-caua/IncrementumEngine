#include "InferusEngine.hpp"

#include <thread>
#include <chrono>
#include <string_view>

#include <spdlog/spdlog.h>

#include "Engine/Core/Input.hpp"
#include "Engine/Core/Window.hpp"
#include "Engine/Core/Camera.hpp"
#include "Engine/ImGuiPacks/MainPack.hpp"
#include "Engine/InferusRenderer/Renderer.hpp"
#include "Engine/Systems/Terrain/TerrainSystem.hpp"
#include "Engine/InferusRenderer/Passes/TerrainRenderer.hpp"

namespace InferusEngine {
    static constexpr std::string_view ENGINE_NAME = "Inferus Engine";
    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;
    static constexpr int TARGET_FPS = 165;
    static constexpr int FOV = 110;

    static constexpr std::chrono::duration<double> FRAME_TARGET_TIME{1.0 / TARGET_FPS};

    bool ShouldClose = false;
    Camera::Camera3D Camera;

    InferusResult Init(){
        auto WindowResult = Window::Create(WIDTH, HEIGHT, ENGINE_NAME.data(), Resize);

        if (WindowResult != InferusResult::SUCCESS) {
            spdlog::error("Window creation failed.");
            return InferusResult::FAIL;
        }

        Input::Create();

        auto RendererResult = Renderer::Create();
        if (RendererResult != InferusResult::SUCCESS) {
            spdlog::error("Inferus Renderer creation failed.");
            return InferusResult::FAIL;
        }

        TerrainSystem::Create(&Camera.Position);
        TerrainRenderer::FeedTerrainSystemPointers();
        Camera = Camera::CreateCamera3D(float(WIDTH)/float(HEIGHT), FOV, nullptr);
        TerrainRenderer::BindCamera(Camera);

        Camera::FlyBy::Create(Camera);
        Camera.Position.y = 10;
        Camera::FlyBy::Update(0);

        return InferusResult::SUCCESS;
    }

    void Destroy() {
        Window::Destroy();
        Input::Destroy();
        TerrainSystem::Destroy();
        Renderer::Destroy();
    }

    void Run() {
        auto LastFrameTime = std::chrono::high_resolution_clock::now();
        while (!ShouldClose && !Window::ShouldClose()) {
            auto FrameBegin = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> DeltaTimeRaw = FrameBegin - LastFrameTime;
            float DeltaTime = DeltaTimeRaw.count();
            LastFrameTime = FrameBegin;

            AnaImGui::OutFps(DeltaTime);

            Camera::FlyBy::Update(DeltaTime);
            Window::Update();
            TerrainSystem::Update();
            Renderer::Render();
            Input::PollInput();

            auto FrameEnd = std::chrono::high_resolution_clock::now();
            auto ElapsedTime = FrameEnd - FrameBegin;

            if ( ElapsedTime < FRAME_TARGET_TIME ) {
                std::this_thread::sleep_for(FRAME_TARGET_TIME - ElapsedTime);
            }
        }
        Window::WaitEvents();
    }

    void Resize(uint32_t Width, uint32_t Height) {
        Renderer::Resize(Width, Height);
        Camera::Resize(Camera, float(Width)/float(Height));
    }
};
