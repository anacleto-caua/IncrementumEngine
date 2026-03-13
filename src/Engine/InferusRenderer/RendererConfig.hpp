#pragma once

#define CONFIG static constexpr

#include <cstdint>
#include <vulkan/vulkan.h>

namespace RendererConfig {
    namespace BufferSystem {
        CONFIG uint32_t DATA_RESERVE_CAPACITY = 100;
        CONFIG uint32_t FREE_INDICES_RESERVE_CAPACITY = 10;
    };
    namespace ImageSystem {
        CONFIG uint32_t DATA_RESERVE_CAPACITY = 100;
        CONFIG uint32_t FREE_INDICES_RESERVE_CAPACITY = 10;
    };
    namespace DepthBuffer {
        CONFIG VkFormat Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    };
};
