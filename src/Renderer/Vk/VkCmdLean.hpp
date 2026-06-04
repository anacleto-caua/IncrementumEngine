#pragma once

#include <vulkan/vulkan.h>

namespace VkCmdLean {
    void Begin(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    void End(VkCommandBuffer cmd);
}

