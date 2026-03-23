#pragma once

#define CONFIG static constexpr

#include <cstdint>
#include <vulkan/vulkan.h>

namespace RendererConfig {
    namespace BufferSystem {
        CONFIG uint32_t RESERVE_CAPACITY = 100;
    };
    namespace ImageSystem {
        CONFIG uint32_t RESERVE_CAPACITY = 100;
        namespace View {
            CONFIG uint32_t RESERVE_CAPACITY = 100;
        }
    };
    namespace DepthBuffer {
        CONFIG VkFormat Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    };
    namespace TransferSystem {
        CONFIG size_t STAGING_BUFFER_SIZE = 1024 * 1024;
    };
};
