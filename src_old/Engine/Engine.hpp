#pragma once

#include "Renderer/Camera.hpp"

struct FrameInfo {
    f32 DeltaTime = 0.0f;
};

struct EngineConfig {
    std::string_view EngineName = "Incrementum Engine";
    u32 Width = 1280;
    u32 Height = 720;
    u32 TargetFps = 165;
    u32 FOV = 110;
};

namespace Engine {
    inline EngineConfig Config;

    IncResult Create(Camera& Camera);
    void Destroy();

    FrameInfo Frame();

    bool ShouldClose();
    void RefreshConfig();
}
