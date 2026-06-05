#include "TransferPipe.hpp"

#include <queue>

#include "RingBuffer.hpp"
#include "Renderer/Vk/Vk.hpp"
#include "Renderer/Vk/VkCmdLean.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

static constexpr u64 STAGING_BUFFER_SIZE = 10 * 1024 * 1024; // 10 MB
static constexpr u64 PARALLEL_TRANSFERS_COUNT = 5;

// Supposed to be harsher than the Vulkan limit of 65536 bytes to avoid bad usage
static constexpr u64 BUFFER_UPDATE_SIZE_LIMIT = 30000;

namespace TransferPipe {

    struct Ticket {
        u32 Value;
        u32 TargetSemaphore; // Point to one of the members in the array bellow
    };
    std::array<TimelineSemaphore, PARALLEL_TRANSFERS_COUNT> SignalSemaphores;
    u32 CurrentSemaphore = 0;

    // Image transfers need another set of semaphores for layer transition and queue ownership transfers
    std::array<TimelineSemaphore, PARALLEL_TRANSFERS_COUNT> ImageTransferSemaphores;
    u32 CurrentImageTransferSemaphore = 0;

    // Tagged union to define each type of package
    enum class PackageType {
        BufferUpdate,
        BufferUpload,
        ImageSliceUpdate,
    };

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

    struct Package {
        PackageType Type;
        u64 Size = 0;
        Ticket TicketToSignal;
        Data Data;
    };

    std::vector<std::queue<Package>> Packages;
    CommandBufferBlock CommandBufferBlock;
    SubmissionPile SubmissionPile;

    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    void Create() {
        StagingBuffer.Create();

        Packages.resize(VkVault::UniqueQueues.size());

        for (auto& semaphore : SignalSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }
        for (auto& semaphore : ImageTransferSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }
    }

    void Destroy() {
        StagingBuffer.Destroy();

        for (auto& semaphore : SignalSemaphores) {
            DestroyTimelineSemaphore(semaphore);
        }
        for (auto& semaphore : ImageTransferSemaphores) {
            DestroyTimelineSemaphore(semaphore);
        }
    }

    Ticket MakeTicket() {
        TimelineSemaphore& semaphore = SignalSemaphores[CurrentSemaphore];
        Ticket ticket = {
            .Value = static_cast<u32>(semaphore.LastSignaledValue),
            .TargetSemaphore = CurrentSemaphore
        };
        CurrentSemaphore = ++CurrentSemaphore % PARALLEL_TRANSFERS_COUNT;
        return ticket;
    }

    bool IsFinished(Ticket ticket) {
        TimelineSemaphore& semaphore = SignalSemaphores[ticket.TargetSemaphore];
        if (semaphore.LastInqueriedValue < ticket.Value) {
            QueryTimelineSemaphoreValue(semaphore);
        }
        return semaphore.LastInqueriedValue > ticket.Value;
    }

    void Frame() {
        // Since we are doing just the fire and forget submission stuff for now
        auto& package_queue = Packages[VkVault::Transfer.ResourceIndex];

        Begin(SubmissionPile);

        while(!package_queue.empty()) {
            Package& package = package_queue.back();
            TimelineSemaphore& semaphore = SignalSemaphores[package.TicketToSignal.TargetSemaphore];

            // Guarantee submission order
            Wait(SubmissionPile, semaphore.Handle, package.TicketToSignal.Value-1);
            Signal(SubmissionPile, semaphore.Handle, package.TicketToSignal.Value);

            // Prepare a command
            VkCommandBuffer cmd = GetNext(CommandBufferBlock);
            VkCmdLean::Begin(cmd);

            // Tagged union stuff
            switch(package.Type) {
                case PackageType::BufferUpdate:
                    {
                        BufferUpdate& update_info = package.Data.BufferUpdate;
                        vkCmdUpdateBuffer(
                            cmd,
                            Buffer::Get(update_info.DstBuffer)->Buffer,
                            update_info.WriteOffset,
                            package.Size,
                            update_info.Src
                        );
                    }
                    break;
                case PackageType::BufferUpload:
                    {
                        BufferUpload& upload_info = package.Data.BufferUpload;

                        VkBufferCopy copy_region {};
                        copy_region.srcOffset = upload_info.ReadOffset;
                        copy_region.dstOffset = upload_info.WriteOffset;
                        copy_region.size = package.Size;

                        vkCmdCopyBuffer(
                            cmd,
                            Buffer::Get(StagingBuffer.Buffer)->Buffer,
                            Buffer::Get(upload_info.DstBuffer)->Buffer,
                            1,
                            &copy_region
                        );
                    }
                    break;
                case PackageType::ImageSliceUpdate:
                    {
                        assert(false && "unimplemented method");
                    }
                    break;
                default:
                    assert(false && "unreachable path has been hit");
                    break;
            }

            // End the command
            VkCmdLean::End(cmd);
            Command(SubmissionPile, cmd);
        }

        // Finish the submission pile
        End(SubmissionPile);
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

        auto ticket = MakeTicket();
        Packages[VkVault::Transfer.ResourceIndex].push(
            {
                .Type = PackageType::BufferUpdate,
                .Size = size,
                .TicketToSignal = ticket,
                .Data = {
                    .BufferUpdate = {
                        .WriteOffset = offset,
                        .DstBuffer = dst,
                        .Src = src
                    }
                }
            }
        );

        return ticket;
    }

    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size, TransferType type) {
        assert(type == TransferType::Normal && "transfer type yet unsupported");

        auto ticket = MakeTicket();
        u64 read_offset = StagingBuffer.Write(src, size);
        Packages[VkVault::Transfer.ResourceIndex].push(
            {
                .Type = PackageType::BufferUpload,
                .Size = size,
                .TicketToSignal = ticket,
                .Data = {
                    .BufferUpload = {
                        .ReadOffset = read_offset,
                        .WriteOffset = write_offset,
                        .DstBuffer = dst
                    }
                }
            });

        return ticket;
    }

    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size, TransferType type) {
        assert(type == TransferType::Normal && "transfer type yet unsupported");

        auto ticket = MakeTicket();
        u64 read_offset = StagingBuffer.Write(src, size);
        Packages[VkVault::Transfer.ResourceIndex].push(
            {
                .Type = PackageType::ImageSliceUpdate,
                .Size = size,
                .TicketToSignal = ticket,
                .Data = {
                    .ImageSliceUpdate = {
                        .DstImage = dst,
                        .TargetLayer = target_layer,
                        .CopyOffset = read_offset
                    }
                }
            });

        return ticket;
    }
}
