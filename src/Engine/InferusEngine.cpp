#include "InferusEngine.hpp"

#include <thread>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "Engine/Core/Input.hpp"
#include "Engine/Core/Window.hpp"
#include "Engine/ImGuiPacks/MainPack.hpp"
#include "Engine/Systems/Terrain/TerrainSystem.hpp"

namespace InferusEngine {
    InferusResult Init(){
        auto WindowResult = Window::Create(WIDTH, HEIGHT, ENGINE_NAME.data(), [](uint32_t w, uint32_t h){Resize(w, h);});

        if (WindowResult != InferusResult::SUCCESS) {
            spdlog::error("Window creation failed.");
            return InferusResult::FAIL;
        }

        Input::Create();

        auto RendererResult = InferusRenderer.Create();
        if (RendererResult != InferusResult::SUCCESS) {
            spdlog::error("Inferus Renderer creation failed.");
            return InferusResult::FAIL;
        }

        TerrainSystem::Create(&Camera.Position);
        InferusRenderer.TerrainRenderer.FeedTerrainSystemPointers();
        Camera.Init(float(WIDTH)/float(HEIGHT), &InferusRenderer.TerrainRenderer.TerrainPushConstants.CameraMVP);

        return InferusResult::SUCCESS;
    }

    void Destroy() {
        Window::Destroy();
        Input::Destroy();
        TerrainSystem::Destroy();
        InferusRenderer.Destroy();
    }

    void Run() {
        auto LastFrameTime = std::chrono::high_resolution_clock::now();
        while (!ShouldClose && !Window::ShouldClose()) {
            auto FrameBegin = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> DeltaTimeRaw = FrameBegin - LastFrameTime;
            float DeltaTime = DeltaTimeRaw.count();
            LastFrameTime = FrameBegin;

            AnaImGui::OutFps(DeltaTime);

            Camera.Update(DeltaTime);
            Window::Update();
            TerrainSystem::Update();
            InferusRenderer.Render();
            Input::PollInput();

            auto FrameEnd = std::chrono::high_resolution_clock::now();
            auto ElapsedTime = FrameBegin - FrameEnd;

            if ( ElapsedTime < FRAME_TARGET_TIME ) {
                std::this_thread::sleep_for(FRAME_TARGET_TIME - ElapsedTime);
            }
        }
        Window::WaitEvents();
    }


    void Resize(uint32_t Width, uint32_t Height) {
        InferusRenderer.Resize(Width, Height);
        Camera.Resize(float(Width)/float(Height));
    }
};
