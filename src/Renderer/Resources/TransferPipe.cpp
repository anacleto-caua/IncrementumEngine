#include "TransferPipe.hpp"

#include <array>
#include <queue>
#include <atomic>
#include <cstring>

#include "ResourceManager.hpp"

static constexpr u64 STAGING_BUFFER_SIZE = 10 * 1024 * 1024;    // 10 MB
static constexpr u32 FRAME_TRANSFER_BUDGET = 2 * 1024 * 1024;   // 2 MB

/**
 * Terribly arbitrary value meant to be harsher than the Vulkan limit of
 * 65536 bytes for vkCmdUpdateBuffer(), to avoid bad usage
 */
static constexpr u32 BUFFER_UPDATE_CAP = 30000;

namespace TransferPipe {

    struct Ticket {
        u64 Value;
    };

    std::atomic<u64> CurrentTicket = { 0 };

    enum class TransferType {
        BufferUpdate,
        BufferCopy,
        ImageSliceUpdate,
    };

    struct BufferCopy {
        u64 ReadOffset = 0;
        u64 WriteOffset = 0;
        Buffer::Id DstBuffer;
        const void* Src = nullptr;
    };

    struct BufferUpdate {
        u64 WriteOffset = 0;
        Buffer::Id DstBuffer;
        const void* Src = nullptr;
    };

    struct ImageSliceUpdate {
        Image::Id DstImage;
        u32 TargetLayer = 0;
        u64 CopyOffset = 0;
    };

    union TransferData {
        BufferCopy BufferCopy;
        BufferUpdate BufferUpdate;
        ImageSliceUpdate ImageSliceUpdate;
    };

    struct Transfer {
        u64 Size;
        TransferType Type;
        TransferData Data;
    };

    namespace StagingBuffer {
        void Create();
        void Destroy();

        void Paste(TransferType type, const void* src, u64 upload_size);
        void Pick(TransferType type, u64 package_size, u64& upload_size1, u64& offset, u64& upload_size2);
    }

    std::queue<Transfer> Queue;

    void Create() {
        StagingBuffer::Create();
    }

    void Destroy() {
        StagingBuffer::Destroy();
        Flush();
    }

    void FrameTransfer(VkCommandBuffer cmd);
    void Flush();

    bool IsFinished(Ticket ticket) {
        return ticket.Value >= CurrentTicket;
    }

    // vkCmdUpdateBuffer
    Ticket QueueBufferUpdate(Buffer::Id dst, const void* data, u64 size, u64 offset) {
        Transfer transfer = {
            .Size = size,
            .Type = TransferType::BufferUpdate,
            .Data = {
                .BufferUpdate = {
                    .WriteOffset = offset,
                    .DstBuffer = dst,
                    .Src = data
                }
            }
        };

        Queue.push(transfer);

        return Ticket { .Value = CurrentTicket + 1 };
    }

    /*
    // vkCmdBufferImageCopy
    Ticket QueueImageSliceUpdate(Image::Id dst, const void* data, u32 target_layer, u64 size);

    // vkCmdBufferCopy
    Ticket QueueBufferUpload(Buffer::Id* dst, const void* data, u64 size);
    */

    namespace StagingBuffer  {
        constexpr auto SIZE = STAGING_BUFFER_SIZE;

        Buffer::Id Buffer;
        u8* MappedHead;
        u8* Head;
        u8* Tail;

        void Create() {
            Buffer::CreateInfo create_info = {
                .Size = SIZE,
                .Type = Buffer::Type::STAGING
            };

            Buffer = Buffer::Add(create_info);
            MappedHead = static_cast<u8*>(Buffer::Map(Buffer::Get(Buffer)->Allocation));
            Head = MappedHead;
            Tail = Head;
        }

        void Destroy() {
            Buffer::Unmap(Buffer::Get(Buffer)->Allocation);
            Buffer::Del(Buffer);
        }

        void Paste(TransferType type, const void* src, u64 upload_size) {
            assert(upload_size < SIZE && "single queued upload is bigger than staging buffer itself");
            u64 neck_size = static_cast<u64>(Head - (MappedHead + SIZE));
            if (upload_size <= neck_size) {
                memcpy(Head, src, upload_size);
                Head += upload_size;
            } else {
                u64 past_neck_upload_size = upload_size - neck_size;

                // Altought it could've been avoided by making a list of non-written actions and
                // saving their data to a dinamyc growing pool I just want to catch some bad usage.
                u64 pre_tail = static_cast<u64>(Tail - (MappedHead));
                assert(pre_tail > past_neck_upload_size && "transfer system staging ring buffer got cluttered");

                // Do not split images, it's messy, just discard the neck.
                // So I don't have to deal with pixel size and padding.
                if (type == TransferType::ImageSliceUpdate) {
                    assert(pre_tail > upload_size && "can't fit whole texture in staging buffer pre-tail");
                    memcpy(MappedHead, src, upload_size);
                    Head = MappedHead + upload_size;
                    return; // Look's like bad flow control
                }

                memcpy(Head, src, neck_size);
                memcpy(MappedHead, (static_cast<const u8*>(src)+past_neck_upload_size), past_neck_upload_size);
                Head = MappedHead + past_neck_upload_size;
            }
        }

        void Pick(TransferType type, u64 package_size, u64& upload_size1, u64& offset, u64& upload_size2) {
            u64 tail_neck_size = static_cast<u64>(Tail - (MappedHead + SIZE));
            if (package_size <= tail_neck_size) {
                upload_size1 = package_size;
                offset = static_cast<u64>(MappedHead - Tail);
                upload_size2 = 0;
                Tail += package_size;
            } else {
                // Special case to account for images not being split
                if (type == TransferType::ImageSliceUpdate) {
                    upload_size1 = package_size;
                    offset = 0;
                    Tail = MappedHead + package_size;
                    return; // Look's like bad flow control
                }
                u64 past_neck_upload_size = package_size - tail_neck_size;
                upload_size1 = tail_neck_size;
                offset =  static_cast<u64>(MappedHead - Tail);
                upload_size2 = past_neck_upload_size;
                Tail = MappedHead + past_neck_upload_size;
            }
        }

    }
}
