#pragma once

#include <array>
#include <vector>

#include "Vk/Vk.hpp"

#define VK_CHECK(expr, ...)                                             \
    do {                                                                \
        if ((expr) != VK_SUCCESS) {                                     \
                analog::error(__VA_ARGS__);                             \
                return IncResult::FAIL;                                 \
            }                                                           \
    } while(0)

namespace RendererConfig {
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

    namespace DepthBuffer {
        static constexpr VkFormat Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    };
}

struct QueueContext {
    u32 Index;
    u32 ResourceIndex; // Direct index to std::vector<QueueResourcePool> QueueResources
    VkQueue Queue = VK_NULL_HANDLE;
};

// This seem's messy and sub-optimal
struct QueueResourcePool {
    VkCommandPool MainCmdPool = VK_NULL_HANDLE;
};

// Vulkan Vault
namespace VkVault {
    inline VkInstance Instance;
    inline VkPhysicalDevice PhysicalDevice;
    inline VkDevice Device;
    inline VmaAllocator VmaAllocator;

    inline VkSurfaceKHR Surface;
    inline VkSurfaceFormatKHR SurfaceFormat;
    inline VkPresentModeKHR PresentMode;

    inline QueueContext Graphics;
    inline QueueContext Present;
    inline QueueContext Transfer;
    inline QueueContext Compute;
    inline std::array<QueueContext*, 4> Queues = {{ &Graphics, &Present, &Transfer, &Compute }};
    inline std::vector<QueueResourcePool> QueueResources;
    inline std::vector<QueueContext*> UniqueQueues;

    inline constexpr u32 COLOR_ATTACHMENT_FORMAT_COUNT = 1;
    inline std::array<VkFormat, COLOR_ATTACHMENT_FORMAT_COUNT> ColorAttachmentFormats { }; // Will be filled by the SurfaceFormat.format
    inline std::array<VkPipelineColorBlendAttachmentState, COLOR_ATTACHMENT_FORMAT_COUNT> ColorBlendAttachmentState = {{
        {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT
        }
    }};

    IncResult Create();
    void Destroy();

    VkCommandBuffer SingleTimeCmdBegin(QueueContext& ctx);
    void SingleTimeCmdSubmit(QueueContext& ctx, VkCommandBuffer cmd);

    VkSurfaceCapabilitiesKHR QuerySurfaceCapabilities();
}


/**
 * The data in the section bellow is referent to Renderer Data that shall not be visible to the engine,
 * only to internal renderer workings.
 */
namespace Renderer {
    namespace Swapchain {
        inline u32 ImageCount = 0;
    }

    // Per frame data that is shared between multiple runtime dependencies of the renderer
    // only for "frame()" functions for multiple passes as of now
    struct FrameContext {
        u32 FrameInFlightIndex = 0;
        u32 ImageViewIndex = 0;
        VkCommandBuffer DrawCommand = VK_NULL_HANDLE;
    };
    inline FrameContext CurrentFrameContext;
}

