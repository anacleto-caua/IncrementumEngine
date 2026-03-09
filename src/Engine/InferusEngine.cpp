#include "InferusEngine.hpp"

#include <thread>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "Engine/Core/Input.hpp"
#include "Engine/Core/Window.hpp"
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
            std::chrono::duration<float> DeltaTimeRaw = LastFrameTime - FrameBegin;
            float DeltaTime = DeltaTimeRaw.count();
            LastFrameTime = FrameBegin;

            Camera.Update(DeltaTime);
            Window::Update();
            TerrainSystem::Update();
            InferusRenderer.Render();
            Input::PollInput();

            auto FrameEnd = std::chrono::high_resolution_clock::now();
            auto ElapsedTime = FrameBegin - FrameEnd;
            OutFps(DeltaTime);

            if ( ElapsedTime < FRAME_TARGET_TIME ) {
                std::this_thread::sleep_for(FRAME_TARGET_TIME - ElapsedTime);
            }
        }
        Window::WaitEvents();
    }

    void OutFps(float DeltaTime) {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

        // Position: Bottom-Right
        const float PAD = 10.0f;
        const ImVec2 viewport_pos = ImGui::GetMainViewport()->WorkPos;
        const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
        ImVec2 window_pos = { (viewport_pos.x + viewport_size.x - PAD), (viewport_pos.y + viewport_size.y - PAD) };
        ImVec2 window_pos_pivot = { 1.0f, 1.0f }; // Pivot on bottom-right corner

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

        if (ImGui::Begin("Performance Overlay", nullptr, window_flags)) {
            ImGui::Text("FPS: %.1f (%.3f ms)", 1.0f / DeltaTime, DeltaTime * 1000.0f);
        }
        ImGui::End();
    }

    void Resize(uint32_t Width, uint32_t Height) {
        InferusRenderer.Resize(Width, Height);
        Camera.Resize(float(Width)/float(Height));
    }
};
