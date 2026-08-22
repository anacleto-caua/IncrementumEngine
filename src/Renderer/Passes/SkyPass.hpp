#pragma once

#include <vulkan/vulkan.h>

#include "Pass.hpp"

// Procedural single-scattering Rayleigh + Mie atmosphere, drawn as a fullscreen triangle behind opaque geometry.
class SkyPass : public Pass {
public:
    IncResult Init() override;
    void Destroy() override;
    void Render() override;

private:
    VkPipeline SkyPipeline = VK_NULL_HANDLE;
    VkPipelineLayout SkyPipelineLayout = VK_NULL_HANDLE;
    i32 PrimarySteps = 16;
    i32 SecondarySteps = 8;

    // Slider-friendly form of GRenderer.SunDirection - kept as the source of truth for the ImGui
    // controls (instead of round-tripping through Cartesian every frame) so dragging a slider
    // near the poles doesn't fight azimuth/elevation ambiguity there.
    f32 SunAzimuthDegrees = 0.0f;
    f32 SunElevationDegrees = 0.0f;

    // Optional automated day cycle, off by default. Elevation is kept non-negative (no "night"
    // handling in atmosphere()), animated as abs(sin(phase)) so the sun sweeps overhead and dips
    // toward the horizon without crossing it. Driven by GEngine.CurrentFrame.DeltaTime rather than
    // wall-clock time, so it correctly stays frozen through anything that isn't simulated time
    // (a held debugger breakpoint, a window drag-resize stall) instead of jumping forward on resume.
    bool AnimateSun = true;
    f32 DayLengthSeconds = 120.0f;
    f32 MinElevationDegrees = 10.0f;
    f32 MaxElevationDegrees = 80.0f;
    f32 ElapsedSeconds = 0.0f;

    void OutSkyData();
};
