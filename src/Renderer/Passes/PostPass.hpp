#pragma once

#include <vulkan/vulkan.h>

#include "Pass.hpp"

// The one tonemap: reads the resolved HDR scene color (see Renderer's scene targets) and writes the
// final display color onto the swapchain, as the first pass of the overlay scope. Every scene
// shader outputs unbounded linear radiance; nothing before this pass tonemaps.
class PostPass : public Pass {
public:
    IncResult Init() override;
    void Destroy() override;
    void Render() override;

    // Re-points the descriptor at the (re)created resolved scene color - called by Renderer after
    // it recreates its scene targets on resize (the device is already idle there, so rewriting a
    // descriptor set a frame in flight might be using is safe).
    void RebindSceneColor();

    // Linear multiplier applied before the tonemap curve - live-adjustable from the Renderer
    // debug section.
    f32 Exposure = 1.0f;

private:
    VkPipeline PostPipeline = VK_NULL_HANDLE;
    VkPipelineLayout PostPipelineLayout = VK_NULL_HANDLE;
    VkSampler SceneColorSampler = VK_NULL_HANDLE;
    VkDescriptorSet SceneColorSet = VK_NULL_HANDLE;
};
