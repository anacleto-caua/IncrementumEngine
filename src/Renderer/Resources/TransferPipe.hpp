#pragma once

#include "Image.hpp"
#include "Buffer.hpp"
#include "Renderer/Vk/LeanVk.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"
#include "Renderer/Resources/ResourceManager.hpp"

// This represents both an acquire and release since it's cleaner to just harvest both at the same time
// For this I recommend making all releases in a single command and all acquires on a different different command defined by the ticket bellow
struct ImageOwnershipTransfer {
    Image::Id Image;
    Ticket Released;    // Transfer's acquire waits on this - signaled once the owner's release is actually submitted
    Ticket Written;     // Owner's re-acquire waits on this
    Ticket Acquired;    // Owner's re-acquire signals this
    u32 TargetLayer;
};

namespace TransferPipe {
    // Semaphore backing the tickets used internally to order releases before acquires
    inline TimelineSemaphore LazySemaphore;

    // Command buffers for release/acquire barriers - one per unique queue. Deliberately NOT
    // tied to any frame-in-flight reset cycle (see AcquirePending): a frame's own command block
    // gets reset as soon as its frame-in-flight slot comes back around, but an acquire command
    // recorded this frame can still be legitimately pending well past that (it waits on the
    // Transfer queue, which runs on its own schedule) - resetting a pool out from under a
    // pending buffer is invalid Vulkan usage, not just a style problem.
    inline QueueContainer<CommandBufferBlock> SpecialCommandBufferBlocks;

    IncResult Create();
    void Destroy();
    /**
     * This methods just flushes the entire package queue
     */
    void LazySubmit();

    /**
     * Releases and writes everything currently queued, without blocking and without resetting
     * any command buffers. Use this to kick off a new streaming transfer from outside the
     * render loop (e.g. terrain chunk streaming) - AcquirePending, already called every frame
     * by Renderer::Frame(), picks up the result once the GPU is actually done.
     */
    void SubmitReleaseAndWrite();

    /**
     * Opportunistically resets TransferCommandBufferBlock/SpecialCommandBufferBlocks once their
     * outstanding work has actually finished on the GPU - non-blocking, cheap to call every
     * frame. This is what keeps those pools from growing without bound during streaming.
     */
    void TryReclaimCommandBuffers();

    bool HasDataToAcquire(QueueRole queue);
    ImageOwnershipTransfer PopAwaitingAcquire(QueueRole queue);
    void MarkSpecialBlockUsed(QueueRole role, Ticket ticket);

    /**
     * If anything has ever been re-acquired for "role", fills out_ticket with the most recent
     * such ticket and returns true. A caller about to sample images owned by "role" (e.g. a
     * draw command) should wait on this ticket first - AcquirePending's barrier lives in its own
     * submission entry with no implicit ordering relative to anything else otherwise.
     */
    bool GetLastAcquireTicket(QueueRole role, Ticket& out_ticket);

    /**
     * Re-acquires ownership of image transfers for "queue" that the Transfer queue has already
     * written and released back, writing acquire barriers into "pile" (command buffers come
     * from TransferPipe's own SpecialCommandBufferBlocks, not the caller's - see the comment on
     * that member). Keeps writing while "pile" still has room - it's the caller's job to submit
     * "pile" afterwards, so this can be folded into an already-existing submission (e.g. a
     * frame's own draw submit) instead of paying for a dedicated vkQueueSubmit2.
     */
    template <typename PileT>
    void AcquirePending(QueueRole queue, PileT& pile) {
        CommandBufferBlock& cmd_block = SpecialCommandBufferBlocks[queue];

        auto has_room = [](const PileT& p) {
            return
                p.CmdCount    + 1 <= p.MaxCommandBuffers &&
                p.SubmitCount + 1 <= p.MaxSubmits        &&
                p.WaitCount   + 1 <= p.MaxWaitSemaphores &&
                p.SignalCount + 1 <= p.MaxSignalSemaphores;
        };

        while (has_room(pile) && HasDataToAcquire(queue)) {
            VkCommandBuffer acquire_command = GetNext(cmd_block);
            ImageOwnershipTransfer transfer = PopAwaitingAcquire(queue);
            Image::Value* image_value = Image::Get(transfer.Image);

            VkImageSubresourceRange subresource_range {};
            subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource_range.baseMipLevel = 0;
            subresource_range.levelCount = 1;
            subresource_range.baseArrayLayer = transfer.TargetLayer;
            subresource_range.layerCount = 1;

            LeanVk::BeginCommand(acquire_command);
            VkImageMemoryBarrier2 acquire_on_owner {};
            acquire_on_owner.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            acquire_on_owner.srcStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for acquire
            acquire_on_owner.srcAccessMask = 0;                       // Required for acquire
            acquire_on_owner.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; // Where the owner queue uses Layout X
            acquire_on_owner.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            acquire_on_owner.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            acquire_on_owner.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            acquire_on_owner.srcQueueFamilyIndex = VkVault::Queues[QueueRole::Transfer].FamilyIndex;
            acquire_on_owner.dstQueueFamilyIndex = VkVault::Queues[queue].FamilyIndex;
            acquire_on_owner.image = image_value->Image;
            acquire_on_owner.subresourceRange = subresource_range;

            VkDependencyInfo dep_acquire {};
            dep_acquire.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_acquire.imageMemoryBarrierCount = 1;
            dep_acquire.pImageMemoryBarriers = &acquire_on_owner;
            vkCmdPipelineBarrier2(acquire_command, &dep_acquire);

            LeanVk::EndCommand(acquire_command);

            pile.BeginSubmission();

            pile.WaitForTicket(transfer.Written);
            pile.SignalTicket(transfer.Acquired);

            pile.AddCommand(acquire_command);

            pile.EndSubmission();

            MarkSpecialBlockUsed(queue, transfer.Acquired);
        }
    }

    Ticket QueueBufferUpdate(Buffer::Id dst, u64 offset, u64 size, void* src);
    Ticket QueueBufferUpload(Buffer::Id dst, u64 write_offset, const void* src, u64 size);
    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size);
}
