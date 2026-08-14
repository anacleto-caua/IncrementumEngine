#pragma once

#include "Image.hpp"
#include "Buffer.hpp"

namespace TransferPipe {

    IncResult Create();
    void Destroy();
    /**
     * This methods just flushes the entire package queue
     */
    void LazySubmit();

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src);
    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size);
    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size);
}
