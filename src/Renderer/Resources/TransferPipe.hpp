#pragma once

#include "Image.hpp"
#include "Buffer.hpp"

// This represents both an acquire and release since it's cleaner to just harvest both at the same time
// For this I recommend making all releases in a single command and all acquires on a different different command defined by the ticket bellow
struct ImageOwnershipTransfer {
    Image::Id Image;
    Ticket Written;     // Acquiring wait on this
    Ticket Acquired;    // Acquiring signals this
    u32 TargetLayer;
};

namespace TransferPipe {

    IncResult Create();
    void Destroy();
    /**
     * This methods just flushes the entire package queue
     */
    void LazySubmit();

    bool HasDataToUnroll(QueueContext& queue);
    ImageOwnershipTransfer UnrollImageOwnershipTransfer(QueueContext& queue);

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src);
    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size);
    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size);
}
