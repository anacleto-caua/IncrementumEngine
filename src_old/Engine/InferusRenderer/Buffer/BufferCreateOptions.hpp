#pragma once

#include <array>

#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"

namespace BufferCreateOptions {
    struct BufferOptions {
        VkBufferUsageFlags vkUsage = 0;
        VmaMemoryUsage vmaUsage = VMA_MEMORY_USAGE_UNKNOWN;
        VmaAllocationCreateFlags vmaFlags = 0;
        VkMemoryPropertyFlags requiredFlags = 0;

        constexpr BufferOptions operator+(const BufferOptions& other) const {
            return BufferOptions{
                .vkUsage = this->vkUsage | other.vkUsage,
                .vmaUsage = (this->vmaUsage != VMA_MEMORY_USAGE_UNKNOWN) ? this->vmaUsage : other.vmaUsage,
                .vmaFlags = this->vmaFlags | other.vmaFlags,
                .requiredFlags = this->requiredFlags | other.requiredFlags
            };
        }
   };

    inline constexpr std::array<BufferOptions, static_cast<size_t>(BufferSystem::CreateInfoMemoryType::_BUFFER_MEMORY_TYPE_COUNT_)> BufferMemoryOptions = {
        {
            // GPU_STATIC,
            {
                .vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_UNKNOWN,
                .vmaFlags = 0,
                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            },
            // CPU_TO_GPU,
            {
                .vkUsage = 0,
                .vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .requiredFlags = 0
            },
            // STAGING_UPLOAD,
            {
                .vkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .requiredFlags = 0

            },
            // READBACK
            {
                .vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .requiredFlags = 0
            }
        }
    };

    inline constexpr std::array<BufferOptions, static_cast<size_t>(BufferSystem::CreateInfoUsage::_BUFFER_USAGE_COUNT_)> BufferUsageOptions = {
        {
            // VERTEX
            {
                .vkUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_UNKNOWN,
                .vmaFlags = 0,
                .requiredFlags = 0
            },
            // INDEX
            {
                .vkUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_UNKNOWN,
                .vmaFlags = 0,
                .requiredFlags = 0
            },
            // SSBO
            {
                .vkUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_UNKNOWN,
                .vmaFlags = 0,
                .requiredFlags = 0
            },
            // UBO
            {
                .vkUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .vmaUsage = VMA_MEMORY_USAGE_UNKNOWN,
                .vmaFlags = 0,
                .requiredFlags = 0
            },
            // STAGING
            {
                .vkUsage = 0,
                .vmaUsage = VMA_MEMORY_USAGE_UNKNOWN,
                .vmaFlags = 0,
                .requiredFlags = 0
            }
        }
    };

    inline constexpr BufferOptions GetBufferOptions(BufferSystem::CreateInfoMemoryType memType, BufferSystem::CreateInfoUsage usage) {
        auto MemOptions = BufferMemoryOptions[static_cast<size_t>(memType)];
        auto UsageOptions = BufferUsageOptions[static_cast<size_t>(usage)];
        return BufferOptions{
            .vkUsage = MemOptions.vkUsage | UsageOptions.vkUsage,
            .vmaUsage = (MemOptions.vmaUsage != VMA_MEMORY_USAGE_UNKNOWN) ? MemOptions.vmaUsage : UsageOptions.vmaUsage,
            .vmaFlags = MemOptions.vmaFlags | UsageOptions.vmaFlags,
            .requiredFlags = MemOptions.requiredFlags | UsageOptions.requiredFlags
        };
    }
};
