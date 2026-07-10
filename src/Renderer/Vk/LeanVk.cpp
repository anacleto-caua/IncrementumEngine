#include "LeanVk.hpp"

namespace LeanVk {
    void BeginCommand(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags) {
        VkCommandBufferBeginInfo begin_info {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = flags;

        vkBeginCommandBuffer(cmd, &begin_info);
    }

    void EndCommand(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
    }
}
