#pragma once

#include <vulkan/vulkan.h>

/**
 * The data in this file is  referent to Renderer Data that shall not be visible to the engine,
 * only to internal renderer workings.
 */
namespace Renderer {
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

    namespace Swapchain {
        inline u32 ImageCount = 0;
    }

    namespace DepthBuffer {
        static constexpr VkFormat Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    };

    // Camera UBO Descriptor
    namespace GlobalDescriptors {
        inline std::array<VkDescriptorSet, Renderer::MAX_FRAMES_IN_FLIGHT> Sets = { VK_NULL_HANDLE };
    }

    // Per frame data that is shared between multiple runtime dependencies of the renderer
    // only for "frame()" functions for multiple passes as of now
    struct RenderFrameData {
        u32 FrameInFlightIndex = 0;
        u32 ImageViewIndex = 0;
        VkCommandBuffer DrawCommand = VK_NULL_HANDLE;
    };
    inline RenderFrameData FrameContext;
}


