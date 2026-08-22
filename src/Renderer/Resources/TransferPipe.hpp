#pragma once

#include <queue>

#include "Image.hpp"
#include "Buffer.hpp"
#include "RingBuffer.hpp"
#include "Renderer/Vk/LeanVk.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

// This represents both an acquire and release since it's cleaner to just harvest both at the same time
// For this I recommend making all releases in a single command and all acquires on a different different command defined by the ticket bellow
struct ImageOwnershipTransfer {
    ImageId Image;
    Ticket Released;    // Transfer's acquire waits on this - signaled once the owner's release is actually submitted
    Ticket Written;     // Owner's re-acquire waits on this
    Ticket Acquired;    // Owner's re-acquire signals this
    u32 TargetLayer;
};

class TransferPipe {
public:
    IncResult Init();
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
     * from this pipe's own SpecialCommandBufferBlocks, not the caller's - see the comment on
     * that member). Keeps writing while "pile" still has room - it's the caller's job to submit
     * "pile" afterwards, so this can be folded into an already-existing submission (e.g. a
     * frame's own draw submit) instead of paying for a dedicated vkQueueSubmit2.
     */
    template <typename PileT>
    void AcquirePending(QueueRole queue, PileT& pile) {
        CommandBufferBlock& cmd_block = SpecialCommandBufferBlocks[queue];

        // SubmitCount reserves room for one MORE submission beyond this function's own writes -
        // every real caller (Renderer::Frame(), LazySubmit()'s Stage 3) submits into "pile" via
        // its own BeginSubmission()/EndSubmission() pair right after calling this, so filling the
        // pile to exactly MaxSubmits here would leave that guaranteed follow-up submission with
        // nowhere to go. Hit for real once a high-throughput streaming caller produced more
        // pending re-acquires in one window than this reservation accounted for - the other three
        // capacities don't have the same "one more guaranteed" caller pattern, so they keep their
        // original, tighter bound.
        auto has_room = [](const PileT& p) {
            return
                p.CmdCount    + 1 <= p.MaxCommandBuffers &&
                p.SubmitCount + 2 <= p.MaxSubmits        &&
                p.WaitCount   + 1 <= p.MaxWaitSemaphores &&
                p.SignalCount + 1 <= p.MaxSignalSemaphores;
        };

        while (has_room(pile) && HasDataToAcquire(queue)) {
            VkCommandBuffer acquire_command = cmd_block.GetNext();
            ImageOwnershipTransfer transfer = PopAwaitingAcquire(queue);
            Image* image_value = Images.Get(transfer.Image);

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
            acquire_on_owner.newLayout = image_value->UsageLayout;
            acquire_on_owner.srcQueueFamilyIndex = VkVault::Queues[QueueRole::Transfer].FamilyIndex;
            acquire_on_owner.dstQueueFamilyIndex = VkVault::Queues[queue].FamilyIndex;
            acquire_on_owner.image = image_value->Handle;
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

    Ticket QueueBufferUpdate(BufferId dst, u64 offset, u64 size, void* src);
    Ticket QueueBufferUpload(BufferId dst, u64 write_offset, const void* src, u64 size);
    Ticket QueueImageSliceUpload(ImageId dst, u32 target_layer, const void* src, u64 size);

private:
    static constexpr u64 STAGING_BUFFER_SIZE = 128 * 1024 * 1024; // 128 MB
    static constexpr u64 PARALLEL_TRANSFERS_COUNT = 15;
    // Supposed to be harsher than the Vulkan limit of 65536 bytes to avoid bad usage
    [[maybe_unused]] static constexpr u64 BUFFER_UPDATE_SIZE_LIMIT = 30000;

    // The parameters used for the main transmission pile. LazyWrite() drains PackageQueue into
    // this pile every SubmitReleaseAndWrite() call and stops early once IsFull() trips, leaving
    // the rest (and the images they target) stuck mid-transfer for extra frames - so this must be
    // sized to fit a full streaming burst in one pass. Each TransferAcquireWriteRelease package
    // uses 1 submit, 1 command buffer, 2 wait-semaphore entries, and 1 signal, so WAIT_SEMAPHORES
    // must stay exactly 2x SUBMITS. Bump these together if a burst-sized caller starts overflowing
    // this pile again - same class of capacity bug as AcquirePending's pile reservation above,
    // just one stage earlier in the pipeline.
    static constexpr u64 NORMAL_PILE_SUBMITS = 64;
    static constexpr u64 NORMAL_PILE_COMMAND_BUFFERS = 64;
    static constexpr u64 NORMAL_PILE_WAIT_SEMAPHORES = 128;
    static constexpr u64 NORMAL_PILE_SIGNAL_SEMAPHORES = 64;
    using StandardSubmissionPile = SubmissionPile<
            QueueRole::Transfer,
            NORMAL_PILE_SUBMITS,
            NORMAL_PILE_COMMAND_BUFFERS,
            NORMAL_PILE_WAIT_SEMAPHORES,
            NORMAL_PILE_SIGNAL_SEMAPHORES
        >;

    /**
     * The parameters used for the special transmission piles,
     * since they're used for image transfer exclusively they have twice the size,
     * this will guarantee the normal pile will overflow before and thus will
     * make it easier to check if it's full
    */
    static constexpr u64 SPECIAL_PILE_SUBMITS = NORMAL_PILE_SUBMITS * 20;
    static constexpr u64 SPECIAL_PILE_COMMAND_BUFFERS = NORMAL_PILE_COMMAND_BUFFERS * 20;
    static constexpr u64 SPECIAL_PILE_WAIT_SEMAPHORES = NORMAL_PILE_WAIT_SEMAPHORES * 20;
    static constexpr u64 SPECIAL_PILE_SIGNAL_SEMAPHORES = NORMAL_PILE_SIGNAL_SEMAPHORES * 20;
    // One SpecialSubmissionPile lives per *unique queue family* (see QueueContainer), not per
    // role - which role actually owns a given entry is only known at runtime (it depends on how
    // the current GPU's queue families happen to overlap, resolved by VkVault::PickQueues()), so
    // SubmissionPile's own compile-time ROLE can't describe it truthfully. QueueRole::Graphics
    // here is a placeholder to satisfy the (mandatory) ROLE parameter - nothing reads it, since
    // these piles are always submitted through SubmitToRole() below with an explicit runtime
    // role instead of through SubmissionPile::Submit().
    using SpecialSubmissionPile = SubmissionPile<
            QueueRole::Graphics,
            SPECIAL_PILE_SUBMITS,
            SPECIAL_PILE_COMMAND_BUFFERS,
            SPECIAL_PILE_WAIT_SEMAPHORES,
            SPECIAL_PILE_SIGNAL_SEMAPHORES
        >;

    // Tagged union to define each type of package
    enum class PackageType {
        BufferUpdate,
        BufferUpload,
        TransferAcquireWriteRelease,
    };

    struct BufferUploadPkg {
        u64 ReadOffset = 0;
        u64 WriteOffset = 0;
        BufferId DstBuffer;
    };

    struct BufferUpdatePkg {
        u64 WriteOffset = 0;
        BufferId DstBuffer;
        const void* Src = nullptr;
    };

    struct TransferAcquireWriteReleasePkg {
        ImageId DstImage;
        Ticket ReleasedTicket; // Transfer's acquire waits on this - signaled once the owner's release is submitted
        Ticket AcquiredTicket; // Passed through so this item can be handed to AwaitingAcquire once actually written
        u32 TargetLayer = 0;
        u64 CopyOffset = 0;
        u64 TransferStatusWaitOn = 0;
    };

    union PackageData {
        BufferUpdatePkg BufferUpdate;
        BufferUploadPkg BufferUpload;
        TransferAcquireWriteReleasePkg TransferAcquireWriteRelease;
    };

    struct Package {
        PackageType Type;
        u64 Size = 0;
        Ticket TicketToSignal;
        PackageData Data;
    };

    /**
     * Tracks the last ticket signaled by any work touching a given command buffer pool, so
     * TryReclaimCommandBuffers() can poll IsFinished() and Reset() it once safe, without blocking.
     * Relying on "last ticket done implies everything earlier on this pool is done" only holds
     * because every submission touching a given pool goes to the *same* queue, and queues execute
     * their submitted work in submission order - the same assumption this codebase already leans
     * on elsewhere (e.g. LazySubmit's vkQueueWaitIdle-based cleanup).
     */
    struct ReclaimTracker {
        Ticket LastTicket;
        bool Valid = false;
    };

    // Semaphore backing the tickets used internally to order releases before acquires
    TimelineSemaphore LazySemaphore;

    // Command buffers for release/acquire barriers - one per unique queue. Deliberately NOT
    // tied to any frame-in-flight reset cycle (see AcquirePending): a frame's own command block
    // gets reset as soon as its frame-in-flight slot comes back around, but an acquire command
    // recorded this frame can still be legitimately pending well past that (it waits on the
    // Transfer queue, which runs on its own schedule) - resetting a pool out from under a
    // pending buffer is invalid Vulkan usage, not just a style problem.
    QueueContainer<CommandBufferBlock> SpecialCommandBufferBlocks;

    std::array<TimelineSemaphore, PARALLEL_TRANSFERS_COUNT> SignalSemaphores;
    u32 CurrentSemaphore = 0;

    std::queue<Package> PackageQueue;

    StandardSubmissionPile TransferSubmissionPile;
    CommandBufferBlock TransferCommandBufferBlock;

    // One per queue, as of now it's just for ImageSliceUpdates
    QueueContainer<SpecialSubmissionPile> SpecialSubmissionPiles;

    ReclaimTracker TransferBlockReclaim;                 // TransferCommandBufferBlock (Transfer queue)
    QueueContainer<ReclaimTracker> SpecialBlockReclaim;   // SpecialCommandBufferBlocks[role] (role's own queue)

    // The last ticket AcquirePending signaled for a role, tracked separately from
    // SpecialBlockReclaim above (which also gets touched by releases) - a caller that draws from
    // images owned by "role" needs to wait specifically on this before sampling them. See
    // GetLastAcquireTicket().
    QueueContainer<ReclaimTracker> LastAcquireForRole;

    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    /**
     * Every pending image ownership transfer moves through two queues in order:
     *  AwaitingRelease  - the owner queue hasn't released it to Transfer yet (ReleasePending does this)
     *  AwaitingAcquire  - Transfer has already acquired, written, and released it back; the owner
     *                      just needs to re-acquire it (AcquirePending does this)
     *
     * PendingImageTransfersTimelineStatus is linked to AwaitingRelease: it's basically a timeline
     * semaphore on the cpu side, a forever incrementing integer used like below.
     * Every time a transfer operation related to a image is made the value of the current "PendingImageTransfersTimelineStatus" plus the current size of "AwaitingRelease" is stored into the transfer package,
     * so I can only "write" that package if the new PendingImageTransfersTimelineStatus is equal os bigger than the previously recorded number.
     * Every time an entry in "AwaitingRelease" is released the PendingImageTransfersTimelineStatus gets incremented.
     *
     */
    // TODO: Check how well this system works with images held by the Transfer Queue itself (probably not well)
    QueueContainer<std::queue<ImageOwnershipTransfer>> AwaitingRelease;
    QueueContainer<std::queue<ImageOwnershipTransfer>> AwaitingAcquire;
    QueueContainer<u64> PendingImageTransfersTimelineStatus;

    Ticket MakeTicket();

    // Just write all packages, I need a version of this that controls how much it writes
    void LazyWrite();

    // Stage 1: release everything awaiting release for "queue" into one shared command buffer,
    // then hand each item off to AwaitingAcquire once Transfer is free to pick it up.
    void ReleasePending(QueueRole queue);

    // Generic-queue-role submit for SpecialSubmissionPiles, whose actual target queue isn't
    // known until runtime - not a member of SubmissionPile itself: adding a runtime-role Submit()
    // back onto SubmissionPile would defeat the point of its compile-time queue role (a
    // wrong-queue submit on the normal, single-queue piles becoming a compile error).
    void SubmitToRole(SpecialSubmissionPile& pile, QueueRole role, VkFence execution_fence = VK_NULL_HANDLE);

    bool HasDataToAcquire(QueueRole queue);

    // Use this right before submiting
    ImageOwnershipTransfer PopAwaitingAcquire(QueueRole queue);

    void MarkSpecialBlockUsed(QueueRole role, Ticket ticket);
};
