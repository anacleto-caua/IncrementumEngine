/**
 * This system still holds a critical error where if I queue a image upload and multiple
 * bufferupload's or bufferupdate's after the image slice upload will hold a ticket in a different upload layer
 * menwhile some buffer updates on the current upload layer will be stuck on thoose tickets by the same semaphore.
 */
#pragma once

#include "Image.hpp"
#include "Buffer.hpp"

namespace TransferPipe {

    enum class TransferType {
        Normal,
        FrameSensible   // This transfer will be inserted at the begining of the frame,
                        // worse perf wise, use only for data that needs to be available at the current frame
    };

    IncResult Create();
    void Destroy();
    /**
     * This methods just flushes the entire package queue
     */
    void LazySubmit();

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src, TransferType type = TransferType::Normal);
    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size, TransferType type = TransferType::Normal);
    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size, TransferType type = TransferType::Normal);
}
