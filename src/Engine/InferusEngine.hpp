#pragma once

#include <chrono>
#include <string_view>

#include "Engine/Types.hpp"
#include "Engine/Core/Camera3D.hpp"
#include "Engine/InferusRenderer/InferusRenderer.hpp"

namespace InferusEngine {
    static constexpr std::string_view ENGINE_NAME = "Inferus Engine";
    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;

    static constexpr int TARGET_FPS = 165;
    static constexpr std::chrono::duration<double> FRAME_TARGET_TIME{1.0 / TARGET_FPS};

    inline bool ShouldClose = false;

    inline InferusRenderer InferusRenderer;
    inline Camera3D Camera;

    InferusResult Init();
    void Destroy();

    void Run();
    void Resize(uint32_t Width, uint32_t Height);
};
