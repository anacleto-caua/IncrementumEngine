#pragma once

#include <array>

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#define VK_CHECK(expr, ...)                                             \
    do {                                                                \
        if ((expr) != VK_SUCCESS) {                                     \
                analog::error(__VA_ARGS__);                             \
                return IncResult::FAIL;                                 \
            }                                                           \
    } while(0)

#define CONFIG static constexpr
namespace RendererConfig {
    CONFIG u32 MAX_FRAMES_IN_FLIGHT = 2;

    namespace DepthBuffer {
        CONFIG VkFormat Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    };
}
#undef CONFIG

struct QueueContext {
    u32 Index;
    VkQueue Queue = VK_NULL_HANDLE;
    VkCommandPool MainCmdPool = VK_NULL_HANDLE;
};

namespace VulkanContext {
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

