#pragma once

#include <vulkan/vulkan.h>

namespace LeanVk {
    void BeginCommand(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    void EndCommand(VkCommandBuffer cmd);
}

