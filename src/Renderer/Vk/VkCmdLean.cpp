#include "VkCmdLean.hpp"

namespace VkCmdLean {

    void Begin(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags) {
        VkCommandBufferBeginInfo begin_info {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = flags;

        vkBeginCommandBuffer(cmd, &begin_info);
    }

    void End(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
    }
}
