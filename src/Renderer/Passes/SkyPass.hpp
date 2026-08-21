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

    void OutSkyData();
};
