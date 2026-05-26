#include "TransferPipe.hpp"

#include <queue>

#include "RingBuffer.hpp"

static constexpr u64 STAGING_BUFFER_SIZE = 10 * 1024 * 1024; // 10 MB

// Supposed to be harsher than the Vulkan limit of 65536 bytes to avoid bad usage
static constexpr u64 BUFFER_UPDATE_SIZE_LIMIT = 30000;

namespace TransferPipe {
    struct Ticket {
        u64 Value;
    };

    u64 CurrentTransfer = 0; // The last transfer made
    u64 TransferCounter = 0; // The ammount of transfers registered

    enum class PackageType {
        BufferUpdate,
        BufferCopy,
        ImageSliceUpdate,
    };

    namespace PackageData {
        struct BufferCopy {
            u64 ReadOffset = 0;
            u64 WriteOffset = 0;
            Buffer::Id DstBuffer;
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

        union Data {
            BufferCopy BufferCopy;
            BufferUpdate BufferUpdate;
            ImageSliceUpdate ImageSliceUpdate;
        };
    }

    struct Package {
        PackageType Type;
        u64 Size = 0;
        PackageData::Data Data;
    };

    std::vector<std::queue<Package>> Packages;
    RingBuffer<STAGING_BUFFER_SIZE> RingBuffer;

    void Create() {
        RingBuffer.Create();

        // Wonky but works
        u32 koth = 0;
        for (auto q : VkVault::Queues) {
            if (q->Index > koth) {
                koth = q->Index;
            }
        }
        Packages.resize(koth+1);
    }

    void Destroy() {
        RingBuffer.Destroy();
    }

    bool IsFinished(Ticket ticket) {
        return ticket.Value <= CurrentTransfer;
    }

    Ticket MakeTicket() {
        return { .Value = TransferCounter + 1 };
    }

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src, TransferType Type) {
        assert(Type == TransferType::Normal && "transfer type yet unsupported");
        assert(size < BUFFER_UPDATE_SIZE_LIMIT && "buffer update queued is bigger than self imposed limit");

        // TODO:
        // I'm stil uncertain about this, as this may imply
        // On queueing: copy from RAM to BAR
        // On recording: copy from BAR to RAM to command buffer
        // It also seems problematic to depend on the src pointer being kept safe,
        // so I could consider just copying this to another buffer in RAM
        // --- RingBuffer.Write(src, size);

        Packages[VkVault::Transfer.Index].push(
            {
                .Type = PackageType::BufferUpdate,
                .Size = size,
                .Data = {
                    .BufferUpdate = {
                        .WriteOffset = offset,
                        .DstBuffer = dst,
                        .Src = src
                    }
                }
            }
        );

        return MakeTicket();
    }
}
