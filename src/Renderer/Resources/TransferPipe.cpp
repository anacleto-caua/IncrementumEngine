#include "TransferPipe.hpp"

#include <queue>
#include <vector>

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

    SubmissionPile TransferSubmissionPile;
    CommandBufferBlock TransferCommandBufferBlock;

    // The resource belows are one per queue, as of now it's just for ImageSliceUpdates
    std::vector<SubmissionPile> SpecialSubmissionPiles;
    std::vector<CommandBufferBlock> SpecialCommandBufferBlocks;

    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    void Create() {
        StagingBuffer.Create();

        for (auto& semaphore : SignalSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }
        for (auto& semaphore : ImageTransferSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }

        SpecialSubmissionPiles.resize(VkVault::UniqueQueues.size());
        SpecialCommandBufferBlocks.resize(VkVault::UniqueQueues.size());

        for (QueueContext* q : VkVault::UniqueQueues) {
            Create(SpecialCommandBufferBlocks[q->ResourceIndex], q);
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

        for (auto& block : SpecialCommandBufferBlocks) {
            Destroy(block);
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
        Begin(TransferSubmissionPile);

        while(!PackageQueue.empty()) {
            u64 ring_buffer_read_size = 0;
            Package& package = PackageQueue.back();
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

                        TimelineSemaphore& image_sync_semaphore = ImageTransferSemaphores[CurrentSemaphore++];

                        auto queue_1_family_idx = target_image->OwnerQueue->Index;
                        auto queue_2_family_idx = VkVault::Transfer.Index;

                        SubmissionPile& q1_pile = SpecialSubmissionPiles[target_image->OwnerQueue->ResourceIndex];
                        CommandBufferBlock& q1_block = SpecialCommandBufferBlocks[target_image->OwnerQueue->ResourceIndex];

                        VkImageSubresourceRange subresource_range {};
                        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        subresource_range.baseMipLevel = 0;
                        subresource_range.levelCount = 1;
                        subresource_range.baseArrayLayer = slice_info.TargetLayer;
                        subresource_range.layerCount = 1;

                        // 1. Queue 1 releases
                        VkCommandBuffer cmd_a_q1 = GetNext(q1_block);
                        VkCmdLean::Begin(cmd_a_q1);

                        // Guarantee submission order (on this one semaphore) and make tickets valid
                        Wait(q1_pile, ticket_semaphore.Handle, package.TicketToSignal.Value-1);
                        Wait(q1_pile, image_sync_semaphore);
                        Signal(q1_pile, image_sync_semaphore);

                        VkImageMemoryBarrier2 release_to_q2 {};
                        release_to_q2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        release_to_q2.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // Whatever Q1 was doing
                        release_to_q2.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                        release_to_q2.dstStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for release
                        release_to_q2.dstAccessMask = 0;                       // Required for release
                        release_to_q2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        release_to_q2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        release_to_q2.srcQueueFamilyIndex = queue_1_family_idx;
                        release_to_q2.dstQueueFamilyIndex = queue_2_family_idx;
                        release_to_q2.image = target_image->Image;
                        release_to_q2.subresourceRange = subresource_range;

                        VkDependencyInfo dep_release_1 {};
                        dep_release_1.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dep_release_1.imageMemoryBarrierCount = 1;
                        dep_release_1.pImageMemoryBarriers = &release_to_q2;

                        vkCmdPipelineBarrier2(cmd_a_q1, &dep_release_1);

                        VkCmdLean::End(cmd_a_q1);
                        Command(q1_pile, cmd_a_q1);

                        // 2. Queue 2 - Acquire -> Write -> Release
                        VkCommandBuffer cmd_q2 = GetNext(TransferCommandBufferBlock);
                        VkCmdLean::Begin(cmd_q2);

                        Wait(TransferSubmissionPile, image_sync_semaphore);
                        Signal(TransferSubmissionPile, image_sync_semaphore);

                        VkImageMemoryBarrier2 acquire_on_q2 {};
                        acquire_on_q2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        acquire_on_q2.srcStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for acquire
                        acquire_on_q2.srcAccessMask = 0;                       // Required for acquire
                        acquire_on_q2.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                        acquire_on_q2.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                        acquire_on_q2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;            // EXACT MATCH TO RELEASE 1
                        acquire_on_q2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; // EXACT MATCH TO RELEASE 1
                        acquire_on_q2.srcQueueFamilyIndex = queue_1_family_idx;
                        acquire_on_q2.dstQueueFamilyIndex = queue_2_family_idx;
                        acquire_on_q2.image = target_image->Image;
                        acquire_on_q2.subresourceRange = subresource_range;

                        VkDependencyInfo dep_acquire_2 {};
                        dep_acquire_2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dep_acquire_2.imageMemoryBarrierCount = 1;
                        dep_acquire_2.pImageMemoryBarriers = &acquire_on_q2;
                        vkCmdPipelineBarrier2(cmd_q2, &dep_acquire_2);

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
                            cmd_q2,
                            Buffer::Get(StagingBuffer.Buffer)->Buffer,
                            target_image->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &copy_region
                        );

                        // Queue 2 releases back to Queue 1
                        VkImageMemoryBarrier2 release_to_q1 {};
                        release_to_q1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        release_to_q1.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                        release_to_q1.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                        release_to_q1.dstStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for release
                        release_to_q1.dstAccessMask = 0;                       // Required for release
                        release_to_q1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        release_to_q1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        release_to_q1.srcQueueFamilyIndex = queue_2_family_idx;
                        release_to_q1.dstQueueFamilyIndex = queue_1_family_idx;
                        release_to_q1.image = target_image->Image;
                        release_to_q1.subresourceRange = subresource_range;

                        VkDependencyInfo dep_release_2 {};
                        dep_release_2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dep_release_2.imageMemoryBarrierCount = 1;
                        dep_release_2.pImageMemoryBarriers = &release_to_q1;
                        vkCmdPipelineBarrier2(cmd_q2, &dep_release_2);

                        VkCmdLean::End(cmd_q2);
                        Command(TransferSubmissionPile, cmd_q2);

                        // 3. Queue 1 - Acquires and Migrate to Layout X
                        VkCommandBuffer cmd_b_q1 = GetNext(q1_block);
                        VkCmdLean::Begin(cmd_b_q1);

                        Wait(q1_pile, image_sync_semaphore);
                        Signal(q1_pile, image_sync_semaphore);

                        // Guarantee submission order (on this one semaphore) and make tickets valid
                        Signal(
                            q1_pile,
                            ticket_semaphore.Handle,
                            package.TicketToSignal.Value
                        );

                        VkImageMemoryBarrier2 acquire_on_q1 {};
                        acquire_on_q1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        acquire_on_q1.srcStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for acquire
                        acquire_on_q1.srcAccessMask = 0;                       // Required for acquire
                        acquire_on_q1.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; // Where Queue1 uses Layout X
                        acquire_on_q1.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                        acquire_on_q1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        acquire_on_q1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        acquire_on_q1.srcQueueFamilyIndex = queue_2_family_idx;
                        acquire_on_q1.dstQueueFamilyIndex = queue_1_family_idx;
                        acquire_on_q1.image = target_image->Image;
                        acquire_on_q1.subresourceRange = subresource_range;

                        VkDependencyInfo dep_acquire_1 {};
                        dep_acquire_1.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dep_acquire_1.imageMemoryBarrierCount = 1;
                        dep_acquire_1.pImageMemoryBarriers = &acquire_on_q1;
                        vkCmdPipelineBarrier2(cmd_b_q1, &dep_acquire_1);

                        VkCmdLean::End(cmd_b_q1);
                        Command(q1_pile, cmd_b_q1);
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

    void Flush() {
        assert(false && "unimplemented method");
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
