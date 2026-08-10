#pragma once

#include <array>
#include <vector>

#include "Vk/TimelineSemaphore.hpp"

#define VK_CHECK(expr, ...)                     \
    do {                                        \
        if ((expr) != VK_SUCCESS) {             \
                analog::error(__VA_ARGS__);     \
                return IncResult::FAIL;         \
            }                                   \
    } while(0)

#define VK_OUT(expr, ...)                      \
    do {                                        \
        if ((expr) != VK_SUCCESS) {             \
                analog::error(__VA_ARGS__);     \
        }                                       \
    } while(0)

struct QueueContext {
    u32 Index;
    u32 ResourceIndex; // Direct index to std::vector<QueueResourcePool> QueueResources
    VkQueue Queue = VK_NULL_HANDLE;
};

// This seem's messy and sub-optimal
struct QueueResourcePool {
    VkCommandPool MainCmdPool = VK_NULL_HANDLE;
};

/**
 * Ideally this wouldn't be here, but cpp compilation works a bit too well
 */
namespace VkVault {
    inline std::vector<QueueContext*> UniqueQueues;
}

template <typename T>
class QueueContainer {
private:
    std::vector<T> Resource;

public:
    void Initialize() {
        Resource.resize(VkVault::UniqueQueues.size());
    }

    T& operator[](QueueContext* queue_context) {
        return Resource[queue_context->ResourceIndex];
    }

    const T& operator[](QueueContext* queue_context) const {
        return Resource[queue_context->ResourceIndex];
    }

    auto begin() { return Resource.begin(); }
    auto end() { return Resource.end(); }
    auto begin() const { return Resource.begin(); }
    auto end() const { return Resource.end(); }
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
    inline QueueContainer<QueueResourcePool> QueueResources;
    //inline std::vector<QueueContext*> UniqueQueues; --- declare above thanks to cpp compilation

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
