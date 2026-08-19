#pragma once

#include "Renderer/VkVault.hpp"

constexpr u64 DEFAULT_COMMAND_BUFFER_RESERVE_COUNT = 16;

struct CommandBufferBlock {
    VkCommandPool Pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> Buffers;
    u32 UsedCount = 0;

    CommandBufferBlock() = default;

    IncResult Init(QueueRole role) {
        VkCommandPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = VkVault::Queues[role].FamilyIndex;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        VK_CHECK(vkCreateCommandPool(VkVault::Device, &pool_info, nullptr, &Pool), "couldn't create a command pool");

        Buffers.reserve(DEFAULT_COMMAND_BUFFER_RESERVE_COUNT);
        UsedCount = 0;

        return IncResult::SUCCESS;
    }

    void Destroy() {
        if (Pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(VkVault::Device, Pool, nullptr);
            Pool = VK_NULL_HANDLE;
        }
        Buffers.clear();
    }

    ~CommandBufferBlock() { Destroy(); }

    void Reset() {
        vkResetCommandPool(VkVault::Device, Pool, 0);
        UsedCount = 0;
    }

    VkCommandBuffer GetNext() {
        if (UsedCount < Buffers.size()) {
            return Buffers[UsedCount++];
        }

        VkCommandBufferAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = Pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer new_cmd;
        vkAllocateCommandBuffers(VkVault::Device, &alloc_info, &new_cmd);

        Buffers.push_back(new_cmd);
        UsedCount++;

        return new_cmd;
    }

    CommandBufferBlock(const CommandBufferBlock&) = delete;
    CommandBufferBlock& operator=(const CommandBufferBlock&) = delete;
    CommandBufferBlock(CommandBufferBlock&&) noexcept = default;
    CommandBufferBlock& operator=(CommandBufferBlock&&) noexcept = default;
};
