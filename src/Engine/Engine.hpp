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

    // Benchmark mode: skip the frame-pacing sleep entirely, so the measured frame time is the
    // real work instead of the TargetFps cap.
    bool Uncapped = false;
    // TaskScheduler worker count, 0 = its own default.
    u32 Workers = 0;
};

class Engine {
public:
    EngineConfig Config = {};
    FrameTimer Timer = { Config.TargetFps };

    // Owned, not an independent global - Renderer has real RAII sub-members (TimelineSemaphore,
    // BinarySemaphore, CommandBufferBlock), so its teardown order benefits from a guaranteed
    // owner. Reached ambiently via the GRenderer alias in Game/Game.hpp, same as before.
    Renderer Renderer;

    // Set at the top of Frame(), before Platform::Update()/Renderer.Frame() run - so anything
    // called from deeper in that same call chain (e.g. SkyPass::Render()) can read this frame's
    // DeltaTime ambiently (GEngine.CurrentFrame.DeltaTime) instead of needing it threaded through
    // as a parameter, or falling back to real wall-clock time.
    FrameInfo CurrentFrame;

    IncResult Init(Camera& camera);
    void Destroy();

    void Frame();

    bool ShouldClose();
    void RefreshConfig();

private:
    void ResizeEvent(i32 width, i32 height);
};
