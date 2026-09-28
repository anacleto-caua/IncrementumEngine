#pragma once

#include <vulkan/vulkan.h>

// Split out of Renderer.hpp specifically to avoid a cycle: Renderer now owns TerrainPass as a
// real member (needs TerrainPass.hpp's complete type), and TerrainPass needs these two constants
// at header-parse time (fixed-size per-frame arrays) - a value constant can't be forward-declared
// the way a pointer/reference member can, so it has to live somewhere both headers can reach
// without needing each other. Renderer.hpp re-exposes both as `Renderer::MAX_FRAMES_IN_FLIGHT`/
// `Renderer::DepthBufferFormat` so every existing call site keeps working unchanged.
namespace RendererConstants {
    constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;
    constexpr VkFormat DepthBufferFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    // The 3D scene (terrain/props/sky) renders into an HDR, multisampled offscreen target that is
    // resolved and then tonemapped once by PostPass onto the swapchain. RGBA16F keeps unbounded
    // linear radiance (sky, sun glints) intact until that single tonemap; 4x MSAA is the smallest
    // count Vulkan guarantees for both color and depth framebuffers.
    constexpr VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkSampleCountFlagBits SceneSampleCount = VK_SAMPLE_COUNT_4_BIT;
}
