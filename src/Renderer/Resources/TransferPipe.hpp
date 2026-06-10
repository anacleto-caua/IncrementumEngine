#pragma once

#include "Image.hpp"
#include "Buffer.hpp"

namespace TransferPipe {
    struct Ticket {
        u32 Value;
        u32 TargetSemaphore; // Point to one of the members in the SignalSemaphores array
    };

    enum class TransferType {
        Normal,
        FrameSensible   // This transfer will be inserted at the begining of the frame,
                        // worse perf wise, use only for data that needs to be available at the current frame
    };

    void Create();
    void Destroy();

    bool IsFinished(Ticket ticket);

    /**
     * Prepares the frames command buffer:
     * One per Queue Family(Currently only using Graphics and Transfer)
     */
    void Frame();

    void Flush();

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src, TransferType type = TransferType::Normal);
    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size, TransferType type = TransferType::Normal);
    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size, TransferType type = TransferType::Normal);
}
