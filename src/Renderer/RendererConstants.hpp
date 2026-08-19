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
}
