#include "TransferPipe.hpp"

#include <cassert>

#include "Renderer/Vk/LeanVk.hpp"

IncResult TransferPipe::Init() {
    INC_CHECK(StagingBuffer.Create(), "staging buffer creation failed");

    for (auto& semaphore : SignalSemaphores) {
        INC_CHECK(semaphore.Init(), "transfer pipe signal semaphore creation failed");
    }

    TransferSubmissionPile.Reset();
    INC_CHECK(TransferCommandBufferBlock.Init(QueueRole::Transfer), "transfer command buffer block creation failed");

    INC_CHECK(LazySemaphore.Init(), "transfer pipe lazy semaphore creation failed");

    SpecialSubmissionPiles.Initialize();
    for (auto &pile : SpecialSubmissionPiles) {
        pile.Reset();
    }

    SpecialCommandBufferBlocks.Initialize();
    for (QueueRole role : VkVault::UniqueRoles) {
        INC_CHECK(SpecialCommandBufferBlocks[role].Init(role), "special command buffer block creation failed");
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

void TransferPipe::Destroy() {
    StagingBuffer.Destroy();

    for (auto& semaphore : SignalSemaphores) {
        semaphore.Destroy();
    }
    LazySemaphore.Destroy();

    TransferCommandBufferBlock.Destroy();

    for (auto& block : SpecialCommandBufferBlocks) {
        block.Destroy();
    }
}

Ticket TransferPipe::MakeTicket() {
    Ticket ticket = SignalSemaphores[CurrentSemaphore].CreateTicket();
    CurrentSemaphore = (CurrentSemaphore + 1) % PARALLEL_TRANSFERS_COUNT;
    return ticket;
}

void TransferPipe::SubmitToRole(SpecialSubmissionPile& pile, QueueRole role, VkFence execution_fence) {
    if (pile.SubmitCount > 0) {
        VK_OUT(vkQueueSubmit2(VkVault::Queues[role].Queue, static_cast<u32>(pile.SubmitCount), pile.Submits.data(), execution_fence), "pile submission failed");
        pile.Reset();
    }
}

// Just write all packages, I need a version of this that controls how much it writes
void TransferPipe::LazyWrite() {
    while(!PackageQueue.empty() && !TransferSubmissionPile.IsFull()) {
        bool could_write_package = true;
        u64 ring_buffer_read_size = 0;
        Package package = PackageQueue.front();

        switch(package.Type) {
            case PackageType::BufferUpdate:
                {
                    BufferUpdatePkg& update_info = package.Data.BufferUpdate;

                    TransferSubmissionPile.BeginSubmission();
                    VkCommandBuffer cmd = TransferCommandBufferBlock.GetNext();
                    LeanVk::BeginCommand(cmd);

                    // Guarantee submission order (on this one semaphore) and make tickets valid
                    TransferSubmissionPile.WaitAndSignalTicket(package.TicketToSignal);

                    vkCmdUpdateBuffer(
                        cmd,
                        Buffers.Get(update_info.DstBuffer)->Handle,
                        update_info.WriteOffset,
                        package.Size,
                        update_info.Src
                    );

                    LeanVk::EndCommand(cmd);
                    TransferSubmissionPile.AddCommand(cmd);
                    TransferSubmissionPile.EndSubmission();
                }
                break;
            case PackageType::BufferUpload:
                {
                    BufferUploadPkg& upload_info = package.Data.BufferUpload;
                    ring_buffer_read_size += package.Size;

                    TransferSubmissionPile.BeginSubmission();
                    VkCommandBuffer cmd = TransferCommandBufferBlock.GetNext();
                    LeanVk::BeginCommand(cmd);

                    // Guarantee submission order (on this one semaphore) and make tickets valid
                    TransferSubmissionPile.WaitAndSignalTicket(package.TicketToSignal);

                    VkBufferCopy copy_region {};
                    copy_region.srcOffset = upload_info.ReadOffset;
                    copy_region.dstOffset = upload_info.WriteOffset;
                    copy_region.size = package.Size;

                    vkCmdCopyBuffer(
                        cmd,
                        Buffers.Get(StagingBuffer.Buffer)->Handle,
                        Buffers.Get(upload_info.DstBuffer)->Handle,
                        1,
                        &copy_region
                    );

                    LeanVk::EndCommand(cmd);
                    TransferSubmissionPile.AddCommand(cmd);
                    TransferSubmissionPile.EndSubmission();
                }
                break;
            case PackageType::TransferAcquireWriteRelease:
                {
                    // 2. Queue 2 - Acquire -> Write -> Release
                    TransferAcquireWriteReleasePkg& write_info = package.Data.TransferAcquireWriteRelease;

                    Image* target_image = Images.Get(write_info.DstImage);

                    if (write_info.TransferStatusWaitOn >= PendingImageTransfersTimelineStatus[target_image->OwnerQueue]) {
                        // This item hasn't actually been released yet - break out of the whole
                        // while loop, we need a specific queue to transfer.
                        could_write_package = false;
                        break;
                    }

                    ring_buffer_read_size += package.Size;

                    auto queue_1_family_idx = VkVault::Queues[target_image->OwnerQueue].FamilyIndex;
                    auto queue_2_family_idx = VkVault::Queues[QueueRole::Transfer].FamilyIndex;

                    VkImageSubresourceRange subresource_range {};
                    subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    subresource_range.baseMipLevel = 0;
                    subresource_range.levelCount = 1;
                    subresource_range.baseArrayLayer = write_info.TargetLayer;
                    subresource_range.layerCount = 1;

                    TransferSubmissionPile.BeginSubmission();
                    VkCommandBuffer cmd_q2 = TransferCommandBufferBlock.GetNext();
                    LeanVk::BeginCommand(cmd_q2);

                    TransferSubmissionPile.WaitAndSignalTicket(package.TicketToSignal);
                    // Wait for the owner's release to actually be submitted before Transfer acquires -
                    // without this, only CPU submission order links them, not a real GPU dependency.
                    TransferSubmissionPile.WaitForTicket(write_info.ReleasedTicket);

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
                    acquire_on_q2.image = target_image->Handle;
                    acquire_on_q2.subresourceRange = subresource_range;

                    VkDependencyInfo dep_acquire_2 {};
                    dep_acquire_2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dep_acquire_2.imageMemoryBarrierCount = 1;
                    dep_acquire_2.pImageMemoryBarriers = &acquire_on_q2;
                    vkCmdPipelineBarrier2(cmd_q2, &dep_acquire_2);

                    // Actually writes to the image
                    VkBufferImageCopy copy_region{};
                    copy_region.bufferOffset = write_info.CopyOffset;
                    copy_region.bufferRowLength = 0;   // 0 means tightly packed
                    copy_region.bufferImageHeight = 0; // 0 means tightly packed
                    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copy_region.imageSubresource.mipLevel = 0;
                    copy_region.imageSubresource.baseArrayLayer = write_info.TargetLayer;
                    copy_region.imageSubresource.layerCount = 1;
                    copy_region.imageOffset = {0, 0, 0};
                    copy_region.imageExtent = {target_image->Width, target_image->Height, 1};

                    vkCmdCopyBufferToImage(
                        cmd_q2,
                        Buffers.Get(StagingBuffer.Buffer)->Handle,
                        target_image->Handle,
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
                    release_to_q1.newLayout = target_image->UsageLayout;
                    release_to_q1.srcQueueFamilyIndex = queue_2_family_idx;
                    release_to_q1.dstQueueFamilyIndex = queue_1_family_idx;
                    release_to_q1.image = target_image->Handle;
                    release_to_q1.subresourceRange = subresource_range;

                    VkDependencyInfo dep_release_2 {};
                    dep_release_2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dep_release_2.imageMemoryBarrierCount = 1;
                    dep_release_2.pImageMemoryBarriers = &release_to_q1;
                    vkCmdPipelineBarrier2(cmd_q2, &dep_release_2);

                    LeanVk::EndCommand(cmd_q2);
                    TransferSubmissionPile.AddCommand(cmd_q2);
                    TransferSubmissionPile.EndSubmission();

                    // Only now is this item actually ready for the owner queue to re-acquire
                    AwaitingAcquire[target_image->OwnerQueue].push(
                        {
                            .Image = write_info.DstImage,
                            .Released = write_info.ReleasedTicket,
                            .Written = package.TicketToSignal,
                            .Acquired = write_info.AcquiredTicket,
                            .TargetLayer = write_info.TargetLayer
                        }
                    );
                }
                break;
            default:
                assert(false && "unreachable path has been hit");
                break;
        }
        // Just pop if we were able to execute the corresponding package
        if (!could_write_package) {
            break;
        }
        TransferBlockReclaim = { package.TicketToSignal, true };
        PackageQueue.pop();
        StagingBuffer.Read(ring_buffer_read_size);
    }
}

// Stage 1: release everything awaiting release for "queue" into one shared command buffer,
// then hand each item off to AwaitingAcquire once Transfer is free to pick it up.
void TransferPipe::ReleasePending(QueueRole queue) {
    SpecialSubmissionPile& pile = SpecialSubmissionPiles[queue];
    CommandBufferBlock& cmd_block = SpecialCommandBufferBlocks[queue];

    VkCommandBuffer release_command = cmd_block.GetNext();
    LeanVk::BeginCommand(release_command);

    pile.BeginSubmission();

    while (pile.SignalCount < pile.MaxSignalSemaphores && !AwaitingRelease[queue].empty()) {
        ImageOwnershipTransfer transfer = AwaitingRelease[queue].front();
        AwaitingRelease[queue].pop();
        PendingImageTransfersTimelineStatus[queue] += 1;

        Image* image_value = Images.Get(transfer.Image);

        VkImageSubresourceRange subresource_range {};
        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = 1;
        subresource_range.baseArrayLayer = transfer.TargetLayer;
        subresource_range.layerCount = 1;

        VkImageMemoryBarrier2 release_to_transfer {};
        release_to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        release_to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // Whatever the owner queue was doing
        release_to_transfer.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        release_to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_NONE; // Required for release
        release_to_transfer.dstAccessMask = 0;                       // Required for release
        release_to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        release_to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        release_to_transfer.srcQueueFamilyIndex = VkVault::Queues[queue].FamilyIndex;
        release_to_transfer.dstQueueFamilyIndex = VkVault::Queues[QueueRole::Transfer].FamilyIndex;
        release_to_transfer.image = image_value->Handle;
        release_to_transfer.subresourceRange = subresource_range;

        VkDependencyInfo dep_release {};
        dep_release.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_release.imageMemoryBarrierCount = 1;
        dep_release.pImageMemoryBarriers = &release_to_transfer;
        vkCmdPipelineBarrier2(release_command, &dep_release);

        pile.SignalTicket(transfer.Released);
        SpecialBlockReclaim[queue] = { transfer.Released, true };
    }

    LeanVk::EndCommand(release_command);
    pile.AddCommand(release_command);
    pile.EndSubmission();
}

// Stages 1+2 only: release anything newly queued, then let the Transfer queue acquire,
// write, and release it back. Non-blocking, no reset - safe to call off the render loop
// whenever new streaming data is queued. The caller doesn't need to do anything else;
// AcquirePending (already piggybacked onto Renderer::Frame()) picks up the result whenever
// the GPU actually gets to it.
void TransferPipe::SubmitReleaseAndWrite() {
    for (QueueRole role : VkVault::UniqueRoles) {
        while (!AwaitingRelease[role].empty()) {
            ReleasePending(role);
            SubmitToRole(SpecialSubmissionPiles[role], role);
        }
    }

    LazyWrite();
    TransferSubmissionPile.Submit(VK_NULL_HANDLE);
}

void TransferPipe::LazySubmit() {
    SubmitReleaseAndWrite();

    // Stage 3: re-acquire everything the Transfer queue just handed back
    for (QueueRole role : VkVault::UniqueRoles) {
        while (HasDataToAcquire(role)) {
            AcquirePending(role, SpecialSubmissionPiles[role]);
            SubmitToRole(SpecialSubmissionPiles[role], role);
        }
    }

    // Block until every queue involved above is actually done, then it's safe to wipe the
    // command buffers we handed out this call.
    for (QueueRole role : VkVault::UniqueRoles) {
        vkQueueWaitIdle(VkVault::Queues[role].Queue);
    }

    TransferCommandBufferBlock.Reset();
    TransferBlockReclaim.Valid = false;
    for (QueueRole role : VkVault::UniqueRoles) {
        SpecialCommandBufferBlocks[role].Reset();
        SpecialBlockReclaim[role].Valid = false;
    }

    /**
     * Step the layers only if the lazy write ended because of the current layer being emptied
     * it could be that the submission pile got full before that.
     */
    if (!PackageQueue.empty()) {
        LazySubmit();
    }
}

// Non-blocking: resets a pool only once its last known submission has actually finished on
// the GPU. Safe to call every frame - most calls will find nothing to do.
void TransferPipe::TryReclaimCommandBuffers() {
    if (TransferBlockReclaim.Valid && TransferBlockReclaim.LastTicket.IsFinished()) {
        TransferCommandBufferBlock.Reset();
        TransferBlockReclaim.Valid = false;
    }

    for (QueueRole role : VkVault::UniqueRoles) {
        ReclaimTracker& tracker = SpecialBlockReclaim[role];
        if (tracker.Valid && tracker.LastTicket.IsFinished()) {
            SpecialCommandBufferBlocks[role].Reset();
            tracker.Valid = false;
        }
    }
}

void TransferPipe::MarkSpecialBlockUsed(QueueRole role, Ticket ticket) {
    SpecialBlockReclaim[role] = { ticket, true };
    LastAcquireForRole[role] = { ticket, true };
}

bool TransferPipe::GetLastAcquireTicket(QueueRole role, Ticket& out_ticket) {
    ReclaimTracker& tracker = LastAcquireForRole[role];
    if (!tracker.Valid) { return false; }
    out_ticket = tracker.LastTicket;
    return true;
}

bool TransferPipe::HasDataToAcquire(QueueRole queue) {
    return !(AwaitingAcquire[queue].empty());
}

// Use this right before submiting
ImageOwnershipTransfer TransferPipe::PopAwaitingAcquire(QueueRole queue) {
    ImageOwnershipTransfer transfer = AwaitingAcquire[queue].front();
    AwaitingAcquire[queue].pop();
    return transfer;
}

Ticket TransferPipe::QueueBufferUpdate(BufferId dst, u64 offset, u64 size, void* src) {
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

Ticket TransferPipe::QueueBufferUpload(BufferId dst, u64 write_offset, const void* src, u64 size) {
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
        }
    );

    return ticket;
}

Ticket TransferPipe::QueueImageSliceUpload(ImageId dst, u32 target_layer, const void* src, u64 size) {
    u64 read_offset = StagingBuffer.Write(src, size);

    Ticket image_released_ticket = MakeTicket();
    Ticket image_writen_ticket = MakeTicket();
    Ticket image_acquired_ticket = MakeTicket();

    Image* image_value = Images.Get(dst);
    QueueRole owner = image_value->OwnerQueue;

    u64 timeline_acquisition_dependancy = PendingImageTransfersTimelineStatus[owner] + AwaitingRelease[owner].size();

    AwaitingRelease[owner].push(
        {
            .Image = dst,
            .Released = image_released_ticket,
            .Written = image_writen_ticket,
            .Acquired = image_acquired_ticket,
            .TargetLayer = target_layer
        }
    );

    PackageQueue.push(
        {
            .Type = PackageType::TransferAcquireWriteRelease,
            .Size = size,
            .TicketToSignal = image_writen_ticket,
            .Data = {
                .TransferAcquireWriteRelease = {
                    .DstImage = dst,
                    .ReleasedTicket = image_released_ticket,
                    .AcquiredTicket = image_acquired_ticket,
                    .TargetLayer = target_layer,
                    .CopyOffset = read_offset,
                    .TransferStatusWaitOn = timeline_acquisition_dependancy
                }
            }
        }
    );

    return image_acquired_ticket;
}
