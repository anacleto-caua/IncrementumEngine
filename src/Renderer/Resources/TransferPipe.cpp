#include "TransferPipe.hpp"

#include <queue>

#include "RingBuffer.hpp"

static constexpr u64 STAGING_BUFFER_SIZE = 10 * 1024 * 1024; // 10 MB
static constexpr u64 PARALLEL_TRANSFERS_COUNT = 5;

// Supposed to be harsher than the Vulkan limit of 65536 bytes to avoid bad usage
static constexpr u64 BUFFER_UPDATE_SIZE_LIMIT = 30000;

namespace TransferPipe {
    enum class PackageType {
        BufferUpdate,
        BufferUpload,
        ImageSliceUpdate,
    };

    namespace PackageData {
        struct BufferUpload {
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
            BufferUpdate BufferUpdate;
            BufferUpload BufferUpload;
            ImageSliceUpdate ImageSliceUpdate;
        };
    }

    struct Package {
        PackageType Type;
        u64 Size = 0;
        PackageData::Data Data;
    };

    struct Ticket {
        u32 Value;
        u32 TargetSemaphore; // Point to one of the members in the array bellow
    };

    std::array<TimelineSemaphore, PARALLEL_TRANSFERS_COUNT> Semaphores;
    u32 CurrentSemaphore = 0;

    std::vector<std::queue<Package>> Packages;
    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    void Create() {
        StagingBuffer.Create();

        Packages.resize(VkVault::UniqueQueues.size());

        for (auto semaphore : Semaphores) {
            semaphore = CreateTimelineSemaphore();
        }
    }

    void Destroy() {
        StagingBuffer.Destroy();

        for (auto& semaphore : Semaphores) {
            DestroyTimelineSemaphore(semaphore);
        }
    }

    Ticket MakeTicket() {
        TimelineSemaphore& semaphore = Semaphores[CurrentSemaphore];
        Ticket ticket = {
            .Value = static_cast<u32>(semaphore.LastSignaledValue),
            .TargetSemaphore = CurrentSemaphore
        };
        CurrentSemaphore = ++CurrentSemaphore % PARALLEL_TRANSFERS_COUNT;
        return ticket;
    }

    bool IsFinished(Ticket ticket) {
        TimelineSemaphore& semaphore = Semaphores[ticket.TargetSemaphore];
        if (semaphore.LastInqueriedValue < ticket.Value) {
            QueryTimelineSemaphoreValue(semaphore);
        }
        return semaphore.LastInqueriedValue > ticket.Value;
    }

    void Frame() {
        // ...
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

        Packages[VkVault::Transfer.ResourceIndex].push(
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

    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size, TransferType type) {
        assert(type == TransferType::Normal && "transfer type yet unsupported");

        u64 read_offset = StagingBuffer.Write(src, size);
        Packages[VkVault::Transfer.ResourceIndex].push(
            {
                .Type = PackageType::BufferUpload,
                .Size = size,
                .Data = {
                    .BufferUpload = {
                        .ReadOffset = read_offset,
                        .WriteOffset = write_offset,
                        .DstBuffer = dst
                    }
                }
            });

        return MakeTicket();
    }

    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size, TransferType type) {
        assert(type == TransferType::Normal && "transfer type yet unsupported");

        u64 read_offset = StagingBuffer.Write(src, size);
        Packages[VkVault::Transfer.ResourceIndex].push(
            {
                .Type = PackageType::ImageSliceUpdate,
                .Size = size,
                .Data = {
                    .ImageSliceUpdate = {
                        .DstImage = dst,
                        .TargetLayer = target_layer,
                        .CopyOffset = read_offset
                    }
                }
            });

        return MakeTicket();
    }

}
