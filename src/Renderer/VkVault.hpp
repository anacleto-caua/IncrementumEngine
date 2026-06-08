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

    inline std::array<VkFormat, 1> ColorAttachmentFormats { }; // Will be filled by the SurfaceFormat.format

    IncResult Create();
    void Destroy();

    VkCommandBuffer SingleTimeCmdBegin(QueueContext& ctx);
    void SingleTimeCmdSubmit(QueueContext& ctx, VkCommandBuffer cmd);

    VkSurfaceCapabilitiesKHR QuerySurfaceCapabilities();
}

namespace Renderer {
    namespace Swapchain {
        inline u32 ImageCount = 0;
    }
}

