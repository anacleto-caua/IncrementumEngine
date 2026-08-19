#pragma once

#include <array>
#include <vector>

#include "Vk/TimelineSemaphore.hpp"
#include "Core/EnumIndexedArray.hpp"

// Define this as 1 if you want validation layers
#ifndef INC_ENABLE_VALIDATION_LAYERS
    #define INC_ENABLE_VALIDATION_LAYERS 0
#endif

namespace VkVault {
    // Set per build mode in xmake.lua (debug/releasedbg on, release off). May become a
    // standalone compile flag later, independent of build mode.
    inline constexpr bool EnableValidationLayers = INC_ENABLE_VALIDATION_LAYERS != 0;
}

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

enum class QueueRole : u8 {
    Graphics,
    Present,
    Transfer,
    Compute,

    _COUNT_
};

struct QueueValue {
    VkQueue Queue = VK_NULL_HANDLE;
    u32 FamilyIndex = 0;
    u8 UniqueFamilyId = 0; // Which slot in VkVault::QueueResources this role's family resources live in
};

/**
 * Ideally this wouldn't be here, but cpp compilation works a bit too well
 */
namespace VkVault {
    inline EnumIndexedArray<QueueValue, QueueRole, QueueRole::_COUNT_> Queues;
    inline u32 UniqueFamilyCount = 0;
    inline std::vector<QueueRole> UniqueRoles; // one representative role per unique physical queue family
}

template <typename T>
class QueueContainer {
private:
    std::vector<T> Resource;

public:
    void Initialize() {
        Resource.resize(VkVault::UniqueFamilyCount);
    }

    T& operator[](QueueRole role) {
        return Resource[VkVault::Queues[role].UniqueFamilyId];
    }

    const T& operator[](QueueRole role) const {
        return Resource[VkVault::Queues[role].UniqueFamilyId];
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

    VkSurfaceCapabilitiesKHR QuerySurfaceCapabilities();
}
