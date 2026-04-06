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
        CONFIG uint64_t STAGING_BUFFER_SIZE = 10 * 1024 * 1024;
        CONFIG uint32_t FRAME_TRANSFER_BUDGET = 2 * 1024 * 1024;

        /**
         * Terribly arbitrary value meant to be harsher than the Vulkan limit of
         * 65536 bytes for vkCmdUpdateBuffer(), to avoid bad usage
         */
        CONFIG uint32_t BUFFER_UPDATE_CAP = 30000;
    };
};
