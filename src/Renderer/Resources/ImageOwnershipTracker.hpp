#pragma once

#include <queue>

#include "Image.hpp"
#include "Renderer/Vk/LeanVk.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

// One round trip of an image between its owning queue and whichever queue actually performs
// work on it while it's on loan.
struct ImageOwnershipTransfer {
    ImageId Image;
    Ticket Released;    // acting queue's acquire waits on this - signaled once the owner's release is actually submitted
    Ticket Written;     // owner's re-acquire waits on this
    Ticket Acquired;    // owner's re-acquire signals this
    u32 TargetLayer;
};

// Generic owner<->acting-queue image ownership round trip: the "owner" is whichever queue role
// holds an image between frames (e.g. Graphics, sampling it), and the "acting queue" is whichever
// queue role temporarily borrows it to do work (e.g. Transfer copying into it, or a compute queue
// writing into it as a storage image). Every queue-role-specific detail - which queue acts, the
// layout it leaves the image in, and the stage/access the owner will read it with - is a parameter
// of ReleasePending()/AcquirePending() rather than baked into this type, so the same mechanism
// serves any pairing of owner and acting queue. Template params size this instance's own internal
// SubmissionPile/CommandBufferBlock storage (per unique physical queue family) - each owning
// system (TransferPipe, ComputePipe, ...) sizes its own instance for its own expected burst size.
template <u64 SpecialSubmits, u64 SpecialCmdBufs, u64 SpecialWaits, u64 SpecialSignals>
class ImageOwnershipTracker {
public:
    IncResult Init() {
        SpecialSubmissionPiles.Initialize();
        for (auto& pile : SpecialSubmissionPiles) {
            pile.Reset();
        }

        SpecialCommandBufferBlocks.Initialize();
        for (QueueRole role : VkVault::UniqueRoles) {
            INC_CHECK(SpecialCommandBufferBlocks[role].Init(role), "image ownership tracker command buffer block creation failed");
        }

        SpecialBlockReclaim.Initialize();
        LastAcquireForRole.Initialize();
        AwaitingRelease.Initialize();
        AwaitingAcquire.Initialize();

        PendingImageTransfersTimelineStatus.Initialize();
        for (u64& e : PendingImageTransfersTimelineStatus) {
            e = 0;
        }

        return IncResult::SUCCESS;
    }

    void Destroy() {
        for (auto& block : SpecialCommandBufferBlocks) {
            block.Destroy();
        }
    }

    // --- Release side (owner -> acting queue) ---

    // Snapshot of "how many releases for `owner` are already queued/in-flight" at the moment of
    // queueing - call this BEFORE QueueRelease() to record a CPU-side ordering gate (the acting
    // queue's own write logic can only proceed once ReleaseStatus() has actually caught up to
    // this value - see TransferPipe::QueueImageSliceUpload/LazyWrite for the pattern).
    u64 PendingReleaseDependency(QueueRole owner) const {
        return PendingImageTransfersTimelineStatus[owner] + AwaitingRelease[owner].size();
    }

    // Live value of the same counter, bumped every time ReleasePending() actually flushes an entry.
    u64 ReleaseStatus(QueueRole owner) const {
        return PendingImageTransfersTimelineStatus[owner];
    }

    void QueueRelease(QueueRole owner, const ImageOwnershipTransfer& transfer) {
        AwaitingRelease[owner].push(transfer);
    }

    bool HasPendingRelease(QueueRole owner) const {
        return !AwaitingRelease[owner].empty();
    }

    // Records owner -> acting_queue release barriers for everything queued for "owner", into this
    // tracker's own per-owner SpecialPile - up to SignalCount capacity. `acting_layout` is the
    // layout the acting queue needs to see the image in (e.g. TRANSFER_DST_OPTIMAL for a copy
    // destination, GENERAL for a compute storage-image write). srcStageMask/srcAccessMask stay a
    // deliberately conservative "whatever the owner queue was doing", since the owner's specific
    // prior usage isn't known here.
    void ReleasePending(QueueRole owner, QueueRole acting_queue, VkImageLayout acting_layout) {
        SpecialPile& pile = SpecialSubmissionPiles[owner];
        CommandBufferBlock& cmd_block = SpecialCommandBufferBlocks[owner];

        VkCommandBuffer release_command = cmd_block.GetNext();
        LeanVk::BeginCommand(release_command);

        pile.BeginSubmission();

        while (pile.SignalCount < pile.MaxSignalSemaphores && !AwaitingRelease[owner].empty()) {
            ImageOwnershipTransfer transfer = AwaitingRelease[owner].front();
            AwaitingRelease[owner].pop();
            PendingImageTransfersTimelineStatus[owner] += 1;

            Image* image_value = Images.Get(transfer.Image);

            VkImageSubresourceRange subresource_range {};
            subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource_range.baseMipLevel = 0;
            subresource_range.levelCount = 1;
            subresource_range.baseArrayLayer = transfer.TargetLayer;
            subresource_range.layerCount = 1;

            VkImageMemoryBarrier2 release_to_acting {};
            release_to_acting.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            release_to_acting.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // Whatever the owner queue was doing
            release_to_acting.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            release_to_acting.dstStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for release
            release_to_acting.dstAccessMask = 0;                       // Required for release
            release_to_acting.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            release_to_acting.newLayout = acting_layout;
            release_to_acting.srcQueueFamilyIndex = VkVault::Queues[owner].FamilyIndex;
            release_to_acting.dstQueueFamilyIndex = VkVault::Queues[acting_queue].FamilyIndex;
            release_to_acting.image = image_value->Handle;
            release_to_acting.subresourceRange = subresource_range;

            VkDependencyInfo dep_release {};
            dep_release.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_release.imageMemoryBarrierCount = 1;
            dep_release.pImageMemoryBarriers = &release_to_acting;
            vkCmdPipelineBarrier2(release_command, &dep_release);

            pile.SignalTicket(transfer.Released);
            SpecialBlockReclaim[owner] = { transfer.Released, true };
        }

        LeanVk::EndCommand(release_command);
        pile.AddCommand(release_command);
        pile.EndSubmission();
    }

    void SubmitReleases(QueueRole owner, VkFence execution_fence = VK_NULL_HANDLE) {
        SubmitToRole(SpecialSubmissionPiles[owner], owner, execution_fence);
    }

    // --- Acquire side (acting queue hands back -> owner re-acquires) ---

    // Called by the acting queue's own write logic once it's actually released the image back
    // (e.g. TransferPipe::LazyWrite's copy case, a future ComputePipe write-to-image case).
    void PushAwaitingAcquire(QueueRole owner, const ImageOwnershipTransfer& transfer) {
        AwaitingAcquire[owner].push(transfer);
    }

    bool HasDataToAcquire(QueueRole owner) const {
        return !AwaitingAcquire[owner].empty();
    }

    // Use this right before submitting.
    ImageOwnershipTransfer PopAwaitingAcquire(QueueRole owner) {
        ImageOwnershipTransfer transfer = AwaitingAcquire[owner].front();
        AwaitingAcquire[owner].pop();
        return transfer;
    }

    // Re-acquires ownership of image transfers for "owner" that "acting_queue" has already
    // written and released back, writing acquire barriers into "pile" - command buffers come from
    // this tracker's own SpecialCommandBufferBlocks, not the caller's. Keeps writing while "pile"
    // still has room - it's the caller's job to submit "pile" afterwards, so this can be folded
    // into an already-existing submission (e.g. a frame's own draw/dispatch submit) instead of
    // paying for a dedicated vkQueueSubmit2. `acquire_from_layout`/`consumer_stage`/
    // `consumer_access` describe the layout the acting queue left the image in and the stage/
    // access the owner will actually use it with (e.g. TRANSFER_DST_OPTIMAL + fragment-shader-read
    // for TransferPipe, GENERAL + whatever a compute-consuming case needs).
    template <typename PileT>
    void AcquirePending(
        QueueRole owner,
        QueueRole acting_queue,
        VkImageLayout acquire_from_layout,
        VkPipelineStageFlags2 consumer_stage,
        VkAccessFlags2 consumer_access,
        PileT& pile
    ) {
        CommandBufferBlock& cmd_block = SpecialCommandBufferBlocks[owner];

        // SubmitCount reserves room for one MORE submission beyond this function's own writes -
        // every real caller submits into "pile" via its own BeginSubmission()/EndSubmission() pair
        // right after calling this, so filling the pile to exactly MaxSubmits here would leave
        // that guaranteed follow-up submission with nowhere to go.
        auto has_room = [](const PileT& p) {
            return
                p.CmdCount    + 1 <= p.MaxCommandBuffers &&
                p.SubmitCount + 2 <= p.MaxSubmits        &&
                p.WaitCount   + 1 <= p.MaxWaitSemaphores &&
                p.SignalCount + 1 <= p.MaxSignalSemaphores;
        };

        while (has_room(pile) && HasDataToAcquire(owner)) {
            VkCommandBuffer acquire_command = cmd_block.GetNext();
            ImageOwnershipTransfer transfer = PopAwaitingAcquire(owner);
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
            acquire_on_owner.dstStageMask = consumer_stage;
            acquire_on_owner.dstAccessMask = consumer_access;
            acquire_on_owner.oldLayout = acquire_from_layout;
            acquire_on_owner.newLayout = image_value->UsageLayout;
            acquire_on_owner.srcQueueFamilyIndex = VkVault::Queues[acting_queue].FamilyIndex;
            acquire_on_owner.dstQueueFamilyIndex = VkVault::Queues[owner].FamilyIndex;
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

            MarkAcquireUsed(owner, transfer.Acquired);
        }
    }

    // Runs AcquirePending against this tracker's OWN internal pile (rather than a caller-supplied
    // external one) and submits it right away - for a caller that wants a self-contained "acquire
    // everything and submit" step (e.g. TransferPipe::LazySubmit's Stage 3) instead of folding the
    // acquire into someone else's already-existing submission (the templated overload above is for
    // that case, e.g. Renderer::Frame() folding it into its own draw submission).
    void RunAndSubmitAcquirePending(
        QueueRole owner,
        QueueRole acting_queue,
        VkImageLayout acquire_from_layout,
        VkPipelineStageFlags2 consumer_stage,
        VkAccessFlags2 consumer_access
    ) {
        AcquirePending(owner, acting_queue, acquire_from_layout, consumer_stage, consumer_access, SpecialSubmissionPiles[owner]);
        SubmitToRole(SpecialSubmissionPiles[owner], owner);
    }

    void MarkAcquireUsed(QueueRole owner, Ticket ticket) {
        SpecialBlockReclaim[owner] = { ticket, true };
        LastAcquireForRole[owner] = { ticket, true };
    }

    // If anything has ever been re-acquired for "owner", fills out_ticket with the most recent
    // such ticket and returns true. A caller about to use resources owned by "owner" (e.g. a draw
    // or dispatch command) should wait on this ticket first - AcquirePending's barrier lives in
    // its own submission entry with no implicit ordering relative to anything else otherwise.
    bool GetLastAcquireTicket(QueueRole owner, Ticket& out_ticket) const {
        const ReclaimTracker& tracker = LastAcquireForRole[owner];
        if (!tracker.Valid) { return false; }
        out_ticket = tracker.LastTicket;
        return true;
    }

    // Non-blocking, cheap to call every frame - resets a pool only once its last known submission
    // has actually finished on the GPU.
    void TryReclaimCommandBuffers() {
        for (QueueRole role : VkVault::UniqueRoles) {
            ReclaimTracker& tracker = SpecialBlockReclaim[role];
            if (tracker.Valid && tracker.LastTicket.IsFinished()) {
                SpecialCommandBufferBlocks[role].Reset();
                tracker.Valid = false;
            }
        }
    }

private:
    // One SpecialPile/CommandBufferBlock lives per *unique queue family* (see QueueContainer), not
    // per role - which role actually owns a given entry is only known at runtime, so the pile's
    // own compile-time ROLE can't describe it truthfully. QueueRole::Graphics here is a placeholder
    // to satisfy the (mandatory) ROLE parameter - nothing reads it, since these piles are always
    // submitted through SubmitToRole() with an explicit runtime role instead of through
    // SubmissionPile::Submit().
    using SpecialPile = SubmissionPile<QueueRole::Graphics, SpecialSubmits, SpecialCmdBufs, SpecialWaits, SpecialSignals>;

    struct ReclaimTracker {
        Ticket LastTicket;
        bool Valid = false;
    };

    // Command buffers for release/acquire barriers - one per unique queue family. Deliberately NOT
    // tied to any frame-in-flight reset cycle: a pending acquire recorded this frame can still be
    // legitimately pending well past that (it waits on the acting queue, which runs on its own
    // schedule) - resetting a pool out from under a pending buffer is invalid Vulkan usage.
    QueueContainer<CommandBufferBlock> SpecialCommandBufferBlocks;
    QueueContainer<SpecialPile> SpecialSubmissionPiles;

    // Every pending image ownership transfer moves through two queues in order:
    //  AwaitingRelease - the owner queue hasn't released it to the acting queue yet (ReleasePending does this)
    //  AwaitingAcquire - the acting queue has already acquired, written, and released it back; the
    //                     owner just needs to re-acquire it (AcquirePending does this)
    QueueContainer<std::queue<ImageOwnershipTransfer>> AwaitingRelease;
    QueueContainer<std::queue<ImageOwnershipTransfer>> AwaitingAcquire;
    // Linked to AwaitingRelease: a forever-incrementing per-owner counter acting like a CPU-side
    // timeline semaphore - see PendingReleaseDependency()/ReleaseStatus().
    QueueContainer<u64> PendingImageTransfersTimelineStatus;

    QueueContainer<ReclaimTracker> SpecialBlockReclaim;  // SpecialCommandBufferBlocks[role] (role's own queue)
    // The last ticket AcquirePending signaled for a role, tracked separately from
    // SpecialBlockReclaim above (which also gets touched by releases) - see GetLastAcquireTicket().
    QueueContainer<ReclaimTracker> LastAcquireForRole;

    // Generic-queue-role submit for SpecialPiles, whose actual target queue isn't known until
    // runtime - not a member of SubmissionPile itself: adding a runtime-role Submit() back onto
    // SubmissionPile would defeat the point of its compile-time queue role (a wrong-queue submit
    // on the normal, single-queue piles becoming a compile error).
    void SubmitToRole(SpecialPile& pile, QueueRole role, VkFence execution_fence = VK_NULL_HANDLE) {
        if (pile.SubmitCount > 0) {
            VK_OUT(vkQueueSubmit2(VkVault::Queues[role].Queue, static_cast<u32>(pile.SubmitCount), pile.Submits.data(), execution_fence), "pile submission failed");
            pile.Reset();
        }
    }
};
