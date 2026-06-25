#pragma once

#include "Image.hpp"
#include "Buffer.hpp"

namespace TransferPipe {
    struct Ticket {
        u64 Value;
        u32 TargetSemaphore; // Point to one of the members in the SignalSemaphores array
    };

    enum class TransferType {
        Normal,
        FrameSensible   // This transfer will be inserted at the begining of the frame,
                        // worse perf wise, use only for data that needs to be available at the current frame
    };

    IncResult Create();
    void Destroy();

    bool IsFinished(Ticket ticket);
    void WaitOn(Ticket ticket);

    /**
     * These two methods are poorly defined without any clear usage or future.
     */
    void Frame();
    void FullSubmit();

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src, TransferType type = TransferType::Normal);
    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size, TransferType type = TransferType::Normal);
    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size, TransferType type = TransferType::Normal);
}
