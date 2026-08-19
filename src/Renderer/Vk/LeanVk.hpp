#pragma once

#include <vulkan/vulkan.h>

namespace LeanVk {
    inline void BeginCommand(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) {
        VkCommandBufferBeginInfo begin_info {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = flags;

        vkBeginCommandBuffer(cmd, &begin_info);
    }

    inline void EndCommand(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
    }

    inline void ResetCommand(VkCommandBuffer cmd) {
        vkResetCommandBuffer(cmd, 0);
    }
}
