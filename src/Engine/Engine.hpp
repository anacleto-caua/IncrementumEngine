#pragma once

#include "Renderer/Renderer.hpp"
#include "Engine/Core/FrameTimer.hpp"

// Forward-declared, not included: Camera.hpp reads GEngine.Config (via Game/Game.hpp), so
// including it here would be circular. Every actual user of Camera's full definition includes
// it directly (Game.cpp, Renderer.cpp) - this class only ever needs Camera& as a parameter type.
struct Camera;

struct FrameInfo {
    f32 DeltaTime = 0.0f;
};

struct EngineConfig {
    std::string_view ApplicationTitle = "Incrementum Engine";
    u32 Width = 1280;
    u32 Height = 720;
    u32 TargetFps = 165;
    u32 FOV = 110;
};

class Engine {
public:
    EngineConfig Config = {};
    FrameTimer Timer = { Config.TargetFps };

    // Owned, not an independent global - Renderer has real RAII sub-members (TimelineSemaphore,
    // BinarySemaphore, CommandBufferBlock), so its teardown order benefits from a guaranteed
    // owner. Reached ambiently via the GRenderer alias in Game/Game.hpp, same as before.
    Renderer Renderer;

    IncResult Init(Camera& camera);
    void Destroy();

    FrameInfo Frame();

    bool ShouldClose();
    void RefreshConfig();

private:
    void ResizeEvent(i32 width, i32 height);
};
