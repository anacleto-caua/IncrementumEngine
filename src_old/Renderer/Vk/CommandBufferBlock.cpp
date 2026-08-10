#include "CommandBufferBlock.hpp"

#include <cassert>

u64 DEFAULT_COMMAND_BUFFER_RESERVE_COUNT = 16;

void Create(CommandBufferBlock& block, QueueContext* ctx) {
    VkCommandPoolCreateInfo pool_info {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = ctx->Index;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    vkCreateCommandPool(VkVault::Device, &pool_info, nullptr, &block.Pool);

    block.Buffers.reserve(DEFAULT_COMMAND_BUFFER_RESERVE_COUNT);
    block.UsedCount = 0;
}

void Destroy(CommandBufferBlock& block) {
    if (block.Pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(VkVault::Device, block.Pool, nullptr);
        block.Pool = VK_NULL_HANDLE;
    }
    block.Buffers.clear();
}

void Reset(CommandBufferBlock& block) {
    vkResetCommandPool(VkVault::Device, block.Pool, 0);
    block.UsedCount = 0;
}

VkCommandBuffer GetNext(CommandBufferBlock& block) {
    if (block.UsedCount < block.Buffers.size()) {
        return block.Buffers[block.UsedCount++];
    }

    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = block.Pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer new_cmd;
    vkAllocateCommandBuffers(VkVault::Device, &alloc_info, &new_cmd);

    block.Buffers.push_back(new_cmd);
    block.UsedCount++;

    return new_cmd;
}
