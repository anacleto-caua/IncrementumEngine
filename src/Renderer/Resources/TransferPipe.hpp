#pragma once

#include <functional>

#include "ResourceManager.hpp"

namespace TransferPipe {
    struct Ticket;

    void Create();
    void Destroy();

    /**
     * This function should auto update the available gpu bandwith
     * using frame time and something akin to AIMD as of now it's not being used
     */
    // void TickAndFlush(float last_frame_time);
    void FrameTransfer(VkCommandBuffer cmd);
    // Forces all transfers to be finished
    void Flush();

    bool IsFinished(Ticket ticket);

    // vkCmdUpdateBuffer
    Ticket QueueBufferUpdate(Buffer::Id dst, const void* data, u64 size, u64 offset);

    // vkCmdBufferImageCopy
    Ticket QueueImageSliceUpdate(Image::Id dst, const void* data, u32 target_layer, u64 size);

    /*
    // vkCmdBufferCopy
    Ticket QueueBufferUpload(BufferSystem::Id* dst, const void* data, u64 size);
    */
}
