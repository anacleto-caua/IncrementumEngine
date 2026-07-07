#include "TransferPipe.hpp"

#include <queue>
#include <cassert>

#include "RingBuffer.hpp"
#include "Renderer/Vk/VkCmdLean.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

// The parameters used for the main transmission pile
static constexpr u64 NORMAL_PILE_SUBMITS = 32;
static constexpr u64 NORMAL_PILE_COMMAND_BUFFERS = 64;
static constexpr u64 NORMAL_PILE_WAIT_SEMAPHORES = 64;
static constexpr u64 NORMAL_PILE_SIGNAL_SEMAPHORES = 64;

/**
 * The parameters used for the special transmission piles,
 * since they're used for image transfer exclusively they have twice the size,
 * make it easier to check if it's full this will guarantee the normal pile will overflow before and thus will
 * make it easier to check if it's full
*/
static constexpr u64 SPECIAL_PILE_SUBMITS = NORMAL_PILE_SUBMITS * 2;
static constexpr u64 SPECIAL_PILE_COMMAND_BUFFERS = NORMAL_PILE_COMMAND_BUFFERS * 2;
static constexpr u64 SPECIAL_PILE_WAIT_SEMAPHORES = NORMAL_PILE_WAIT_SEMAPHORES * 2;
static constexpr u64 SPECIAL_PILE_SIGNAL_SEMAPHORES = NORMAL_PILE_SIGNAL_SEMAPHORES * 2;

static constexpr u64 STAGING_BUFFER_SIZE = 10 * 1024 * 1024; // 10 MB
static constexpr u64 PARALLEL_TRANSFERS_COUNT = 5;

// Supposed to be harsher than the Vulkan limit of 65536 bytes to avoid bad usage
static constexpr u64 BUFFER_UPDATE_SIZE_LIMIT = 30000;

namespace TransferPipe {
    // Just carry a safe copy of the last ticket, used to clean the submissions
    Ticket LastTicket = { 0, 0 };

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

    std::queue<Package> PackageQueue;

    SubmissionPile<
        NORMAL_PILE_SUBMITS,
        NORMAL_PILE_COMMAND_BUFFERS,
        NORMAL_PILE_WAIT_SEMAPHORES,
        NORMAL_PILE_SIGNAL_SEMAPHORES
    > TransferSubmissionPile;
    CommandBufferBlock TransferCommandBufferBlock;

    // The resource belows are one per queue, as of now it's just for ImageSliceUpdates
    std::vector<SubmissionPile<
        SPECIAL_PILE_SUBMITS,
        SPECIAL_PILE_COMMAND_BUFFERS,
        SPECIAL_PILE_WAIT_SEMAPHORES,
        SPECIAL_PILE_SIGNAL_SEMAPHORES
    >> SpecialSubmissionPiles;
    std::vector<CommandBufferBlock> SpecialCommandBufferBlocks;

    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    IncResult Create() {
        StagingBuffer.Create();

        for (auto& semaphore : SignalSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }
        for (auto& semaphore : ImageTransferSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }

        Reset(TransferSubmissionPile);
        Create(TransferCommandBufferBlock, &VkVault::Transfer);

        SpecialSubmissionPiles.resize(VkVault::UniqueQueues.size());
        SpecialCommandBufferBlocks.resize(VkVault::UniqueQueues.size());

        for (QueueContext* q : VkVault::UniqueQueues) {
            Reset(SpecialSubmissionPiles[q->ResourceIndex]);
            Create(SpecialCommandBufferBlocks[q->ResourceIndex], q);
        }

        return IncResult::SUCCESS;
    }

    void Destroy() {
        StagingBuffer.Destroy();

        for (auto& semaphore : SignalSemaphores) {
            DestroyTimelineSemaphore(semaphore);
        }
        for (auto& semaphore : ImageTransferSemaphores) {
            DestroyTimelineSemaphore(semaphore);
        }

        Destroy(TransferCommandBufferBlock);
    }

    Ticket MakeTicket() {
        TimelineSemaphore& semaphore = SignalSemaphores[CurrentSemaphore];
        Ticket ticket = {
            .Value = (++semaphore.LastSignaledValue),
            .TargetSemaphore = CurrentSemaphore
        };
        CurrentSemaphore = (CurrentSemaphore + 1) % PARALLEL_TRANSFERS_COUNT;
        LastTicket = ticket;
        return ticket;
    }

    bool IsFinished(Ticket ticket) {
        TimelineSemaphore& semaphore = SignalSemaphores[ticket.TargetSemaphore];
        if (semaphore.LastInqueriedValue < ticket.Value) {
            QueryTimelineSemaphoreValue(semaphore);
        }
        return semaphore.LastInqueriedValue > ticket.Value;
    }

    void WaitOn(Ticket ticket) {
        WaitOnTimelineSemaphore(SignalSemaphores[ticket.TargetSemaphore], ticket.Value);
    }

    // Just write all packages, I need a version of this that controls how much it writes
    void LazyWrite() {
        Begin(TransferSubmissionPile);

        while(!PackageQueue.empty()) {
            u64 ring_buffer_read_size = 0;
            Package package = PackageQueue.front();
            PackageQueue.pop();
            TimelineSemaphore& ticket_semaphore = SignalSemaphores[package.TicketToSignal.TargetSemaphore];

            // Tagged union stuff
            switch(package.Type) {
                case PackageType::BufferUpdate:
                    {
                        BufferUpdate& update_info = package.Data.BufferUpdate;

                        VkCommandBuffer cmd = GetNext(TransferCommandBufferBlock);
                        VkCmdLean::Begin(cmd);

                        // Guarantee submission order (on this one semaphore) and make tickets valid
                        Wait(TransferSubmissionPile, ticket_semaphore.Handle, package.TicketToSignal.Value-1);
                        Signal(TransferSubmissionPile, ticket_semaphore.Handle, package.TicketToSignal.Value);

                        vkCmdUpdateBuffer(
                            cmd,
                            Buffer::Get(update_info.DstBuffer)->Buffer,
                            update_info.WriteOffset,
                            package.Size,
                            update_info.Src
                        );

                        VkCmdLean::End(cmd);
                        Command(TransferSubmissionPile, cmd);
                    }
                    break;
                case PackageType::BufferUpload:
                    {
                        BufferUpload& upload_info = package.Data.BufferUpload;
                        ring_buffer_read_size += package.Size;

                        VkCommandBuffer cmd = GetNext(TransferCommandBufferBlock);
                        VkCmdLean::Begin(cmd);

                        // Guarantee submission order (on this one semaphore) and make tickets valid
                        Wait(TransferSubmissionPile, ticket_semaphore.Handle, package.TicketToSignal.Value-1);
                        Signal(TransferSubmissionPile, ticket_semaphore.Handle, package.TicketToSignal.Value);

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

                        VkCmdLean::End(cmd);
                        Command(TransferSubmissionPile, cmd);
                    }
                    break;
                case PackageType::ImageSliceUpdate:
                    {
                        ImageSliceUpdate& slice_info = package.Data.ImageSliceUpdate;
                        Image::Value* target_image = Image::Get(slice_info.DstImage);
                        ring_buffer_read_size += package.Size;

                        VkImageSubresourceRange subresource_range {};
                        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        subresource_range.baseMipLevel = 0;
                        subresource_range.levelCount = 1;
                        subresource_range.baseArrayLayer = slice_info.TargetLayer;
                        subresource_range.layerCount = 1;

                        VkCommandBuffer command = GetNext(TransferCommandBufferBlock);
                        VkCmdLean::Begin(command);

                        Wait(
                            TransferSubmissionPile,
                            ticket_semaphore.Handle,
                            package.TicketToSignal.Value-1
                        );

                        // Actually writes to the image
                        VkBufferImageCopy copy_region{};
                        copy_region.bufferOffset = slice_info.CopyOffset;
                        copy_region.bufferRowLength = 0;   // 0 means tightly packed
                        copy_region.bufferImageHeight = 0; // 0 means tightly packed
                        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        copy_region.imageSubresource.mipLevel = 0;
                        copy_region.imageSubresource.baseArrayLayer = slice_info.TargetLayer;
                        copy_region.imageSubresource.layerCount = 1;
                        copy_region.imageOffset = {0, 0, 0};
                        copy_region.imageExtent = {target_image->Width, target_image->Height, 1};

                        vkCmdCopyBufferToImage(
                            command,
                            Buffer::Get(StagingBuffer.Buffer)->Buffer,
                            target_image->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &copy_region
                        );

                        // Guarantee submission order (on this one semaphore) and make tickets valid
                        Signal(
                            TransferSubmissionPile,
                            ticket_semaphore.Handle,
                            package.TicketToSignal.Value
                        );

                        VkCmdLean::End(command);
                        Command(TransferSubmissionPile, command);
                        End(TransferSubmissionPile);
                    }
                    break;
                default:
                    assert(false && "unreachable path has been hit");
                    break;
            }
            StagingBuffer.Read(ring_buffer_read_size);
        }

        // Finish the submission pile
        End(TransferSubmissionPile);
    }

    void LazySubmit() {
        LazyWrite();
        SubmitPile(VkVault::Transfer, TransferSubmissionPile, VK_NULL_HANDLE);

        WaitOn(LastTicket); // To safely wipe command buffers
        Reset(TransferCommandBufferBlock);
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
        PackageQueue.push(
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
        PackageQueue.push(
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
        PackageQueue.push(
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
