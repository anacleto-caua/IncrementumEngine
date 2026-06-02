#pragma once

#include "Renderer/VkVault.hpp"

struct CommandBufferBlock {
    VkCommandPool Pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> Buffers;
    u32 UsedCount = 0;
};

void Create(CommandBufferBlock& block, QueueContext& ctx);
void Destroy(CommandBufferBlock& block);

void Reset(CommandBufferBlock& block);

VkCommandBuffer GetNext(CommandBufferBlock& block);

