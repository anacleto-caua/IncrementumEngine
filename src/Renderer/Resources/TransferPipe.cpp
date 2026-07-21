#include "TransferPipe.hpp"

#include <queue>
#include <cassert>

#include "RingBuffer.hpp"
#include "Renderer/Vk/LeanVk.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

// The parameters used for the main transmission pile
static constexpr u64 NORMAL_PILE_SUBMITS = 32;
static constexpr u64 NORMAL_PILE_COMMAND_BUFFERS = 64;
static constexpr u64 NORMAL_PILE_WAIT_SEMAPHORES = 64;
static constexpr u64 NORMAL_PILE_SIGNAL_SEMAPHORES = 64;
using StandardSubmissionPile = SubmissionPile<
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
using SpecialSubmissionPile = SubmissionPile<
        SPECIAL_PILE_SUBMITS,
        SPECIAL_PILE_COMMAND_BUFFERS,
        SPECIAL_PILE_WAIT_SEMAPHORES,
        SPECIAL_PILE_SIGNAL_SEMAPHORES
    >;

static constexpr u64 STAGING_BUFFER_SIZE = 10 * 1024 * 1024; // 10 MB
static constexpr u64 PARALLEL_TRANSFERS_COUNT = 15;

// Supposed to be harsher than the Vulkan limit of 65536 bytes to avoid bad usage
static constexpr u64 BUFFER_UPDATE_SIZE_LIMIT = 30000;

namespace TransferPipe {
    std::array<TimelineSemaphore, PARALLEL_TRANSFERS_COUNT> SignalSemaphores;
    u32 CurrentSemaphore = 0;

    // Tagged union to define each type of package
    enum class PackageType {
        BufferUpdate,
        BufferUpload,
        //ImageSliceUpdate,
        OwnerRelease,
        TransferAcquireWriteRelease,
        OwnerAcquire,
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

    struct OwnerRelease {
        Ticket ImageReleased;
        Image::Id TargetImage;
        u32 TargetLayer = 0;
    };

    struct TransferAcquireWriteRelease {
        Ticket ImageReleased;
        Ticket ImageWriten;
        Image::Id DstImage;
        u32 TargetLayer = 0;
        u64 CopyOffset = 0;
    };

    struct OwnerAcquire {
        Ticket ImageWriten;
        Image::Id TargetImage;
        u32 TargetLayer = 0;
    };

    union Data {
        BufferUpdate BufferUpdate;
        BufferUpload BufferUpload;
        // ImageSliceUpload
        OwnerRelease OwnerRelease;
        TransferAcquireWriteRelease TransferAcquireWriteRelease;
        OwnerAcquire OwnerAcquire;
    };

    struct Package {
        PackageType Type;
        u64 Size = 0;
        Ticket TicketToSignal;
        Data Data;
    };

    /**
     * This upload layers thing works disgustling like frames in flight
     * There are three to encapsulate the (acquire, release, acquire) routine of uploading images
     */
    static constexpr u32 UPLOAD_LAYERS_COUNT = 3;
    /**
     * An array that constains all the multiple upload layers *ref-1
     * inside this array there's a queue of Packages
     *
     * The upload layers works as follows:
     *  The transfer system will write normal packages(BufferUpdate and BufferUpload) to the current layer - shown in "CurrentUploadLayer"
     *  The system will break ImageSliceUpdate packages into 3 packages (Release, Acquire-Upload-Release, Acquire)
     *  The system will write thoose packages in CurrentUploadLayer, CurrentUploadLayer+1, CurrentUploadLayer+2 (consider wrapping)
     *  The system will read all packages(this gives a perfect slot to add a upload size limitation or so) from the current upload layer
     *  After emptying the current layer the system will advance the CurrentUploadLayer step
     *
     * Refs:
     *      1 - This makes so I can break the image update routine(acquire, release, acquire) into multiple of thoose blocks writing
     *      all of them to this vector and "harvesting" them later, safely knowing which ones have already been submited.
     *      It may seem weird but I need to submit a release before an acquire, can't just let it waiting.
     *      - CurrentUploadLayer - tracks this btw.
     *
     */
    std::array<
        std::queue<Package>,
        UPLOAD_LAYERS_COUNT
    > PackageQueues;

    /**
     * Just carry the last ticket, at the time it's written at LazyWrite
     */
    Ticket TopTicket = { 0, 0 };

    u32 CurrentUploadLayer = 0;

    StandardSubmissionPile TransferSubmissionPile;
    CommandBufferBlock TransferCommandBufferBlock;

    // The resource belows are one per queue, as of now it's just for ImageSliceUpdates
    QueueContainer<SpecialSubmissionPile> SpecialSubmissionPiles;
    QueueContainer<CommandBufferBlock> SpecialCommandBufferBlocks;

    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    IncResult Create() {
        StagingBuffer.Create();

        for (auto& semaphore : SignalSemaphores) {
            semaphore = CreateTimelineSemaphore();
        }

        ResetPile(TransferSubmissionPile);
        Create(TransferCommandBufferBlock, &VkVault::Transfer);

        SpecialSubmissionPiles.Initialize();
        for (auto &pile : SpecialSubmissionPiles) {
            ResetPile(pile);
        }

        SpecialCommandBufferBlocks.Initialize();
        for (QueueContext* q : VkVault::UniqueQueues) {
            Create(SpecialCommandBufferBlocks[q], q);
        }

        return IncResult::SUCCESS;
    }

    void Destroy() {
        StagingBuffer.Destroy();

        for (auto& semaphore : SignalSemaphores) {
            DestroyTimelineSemaphore(semaphore);
        }

        Destroy(TransferCommandBufferBlock);

        for (auto& block : SpecialCommandBufferBlocks) {
            Destroy(block);
        }
    }

    bool ArePackageQueuesFullyEmptied() {
        for (auto& packQ : PackageQueues) {
            if (!packQ.empty()) {
                return false;
            }
        }
        return true;
    }

    Ticket MakeTicket() {
        TimelineSemaphore& semaphore = SignalSemaphores[CurrentSemaphore];
        Ticket ticket = {
            .Value = (++semaphore.LastSignaledValue),
            .TargetSemaphore = CurrentSemaphore
        };
        CurrentSemaphore = (CurrentSemaphore + 1) % PARALLEL_TRANSFERS_COUNT;
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

        while(!PackageQueues[CurrentUploadLayer].empty() && !IsFull(TransferSubmissionPile)) {
            u64 ring_buffer_read_size = 0;
            Package package = PackageQueues[CurrentUploadLayer].front();
            PackageQueues[CurrentUploadLayer].pop();
            TopTicket = package.TicketToSignal;

            TimelineSemaphore& ticket_semaphore = SignalSemaphores[package.TicketToSignal.TargetSemaphore];

            switch(package.Type) {
                case PackageType::BufferUpdate:
                    {
                        BufferUpdate& update_info = package.Data.BufferUpdate;

                        BeginSubmission(TransferSubmissionPile);
                        VkCommandBuffer cmd = GetNext(TransferCommandBufferBlock);
                        LeanVk::BeginCommand(cmd);

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

                        LeanVk::EndCommand(cmd);
                        AddCommandToPile(TransferSubmissionPile, cmd);
                        EndSubmission(TransferSubmissionPile);
                    }
                    break;
                case PackageType::BufferUpload:
                    {
                        BufferUpload& upload_info = package.Data.BufferUpload;
                        ring_buffer_read_size += package.Size;

                        BeginSubmission(TransferSubmissionPile);
                        VkCommandBuffer cmd = GetNext(TransferCommandBufferBlock);
                        LeanVk::BeginCommand(cmd);

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

                        LeanVk::EndCommand(cmd);
                        AddCommandToPile(TransferSubmissionPile, cmd);
                        EndSubmission(TransferSubmissionPile);
                    }
                    break;
                case PackageType::OwnerRelease:
                    {
                        // 1. Queue 1 releases
                        OwnerRelease& release_info = package.Data.OwnerRelease;
                        Image::Value* target_image = Image::Get(release_info.TargetImage);
                        Ticket& image_released = release_info.ImageReleased;

                        auto queue_1_family_idx = target_image->OwnerQueue->Index;
                        auto queue_2_family_idx = VkVault::Transfer.Index;

                        SpecialSubmissionPile& q1_pile = SpecialSubmissionPiles[target_image->OwnerQueue];
                        CommandBufferBlock& q1_block = SpecialCommandBufferBlocks[target_image->OwnerQueue];

                        VkImageSubresourceRange subresource_range {};
                        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        subresource_range.baseMipLevel = 0;
                        subresource_range.levelCount = 1;
                        subresource_range.baseArrayLayer = release_info.TargetLayer;
                        subresource_range.layerCount = 1;

                        BeginSubmission(q1_pile);
                        VkCommandBuffer cmd_a_q1 = GetNext(q1_block);
                        LeanVk::BeginCommand(cmd_a_q1);

                        // Guarantee submission order (on this one semaphore) and make tickets valid
                        Wait(q1_pile, ticket_semaphore.Handle, package.TicketToSignal.Value-1);
                        Wait(q1_pile, SignalSemaphores[image_released.TargetSemaphore].Handle, image_released.Value-1);
                        Signal(q1_pile, SignalSemaphores[image_released.TargetSemaphore].Handle, image_released.Value);

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

                        LeanVk::EndCommand(cmd_a_q1);
                        AddCommandToPile(q1_pile, cmd_a_q1);
                        EndSubmission(q1_pile); // I shall consider not using a single command for a image release
                    }
                    break;
                case PackageType::TransferAcquireWriteRelease:
                    {
                        // 2. Queue 2 - Acquire -> Write -> Release
                        TransferAcquireWriteRelease& write_info = package.Data.TransferAcquireWriteRelease;
                        Image::Value* target_image = Image::Get(write_info.DstImage);
                        Ticket& image_released = write_info.ImageReleased;
                        Ticket& image_writen = write_info.ImageWriten;

                        ring_buffer_read_size += package.Size;

                        auto queue_1_family_idx = target_image->OwnerQueue->Index;
                        auto queue_2_family_idx = VkVault::Transfer.Index;

                        VkImageSubresourceRange subresource_range {};
                        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        subresource_range.baseMipLevel = 0;
                        subresource_range.levelCount = 1;
                        subresource_range.baseArrayLayer = write_info.TargetLayer;
                        subresource_range.layerCount = 1;

                        BeginSubmission(TransferSubmissionPile);
                        VkCommandBuffer cmd_q2 = GetNext(TransferCommandBufferBlock);
                        LeanVk::BeginCommand(cmd_q2);

                        Wait(TransferSubmissionPile, SignalSemaphores[image_released.TargetSemaphore].Handle, image_released.Value);
                        Wait(TransferSubmissionPile, SignalSemaphores[image_writen.TargetSemaphore].Handle, image_writen.Value-1);
                        Signal(TransferSubmissionPile, SignalSemaphores[image_writen.TargetSemaphore].Handle, image_writen.Value);

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

                        LeanVk::EndCommand(cmd_q2);
                        AddCommandToPile(TransferSubmissionPile, cmd_q2);
                        EndSubmission(TransferSubmissionPile);
                    }
                    break;
                case PackageType::OwnerAcquire:
                    {
                        // 3. Queue 1 - Acquires and Migrate to Layout X
                        OwnerAcquire& acquire_info  = package.Data.OwnerAcquire;
                        Image::Value* target_image = Image::Get(acquire_info.TargetImage);

                        Ticket& image_writen = acquire_info.ImageWriten;
                        Ticket& final_ticket = package.TicketToSignal;

                        auto queue_1_family_idx = target_image->OwnerQueue->Index;
                        auto queue_2_family_idx = VkVault::Transfer.Index;

                        SpecialSubmissionPile& q1_pile = SpecialSubmissionPiles[target_image->OwnerQueue];
                        CommandBufferBlock& q1_block = SpecialCommandBufferBlocks[target_image->OwnerQueue];

                        VkImageSubresourceRange subresource_range {};
                        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        subresource_range.baseMipLevel = 0;
                        subresource_range.levelCount = 1;
                        subresource_range.baseArrayLayer = acquire_info.TargetLayer;
                        subresource_range.layerCount = 1;

                        BeginSubmission(q1_pile);
                        VkCommandBuffer cmd_b_q1 = GetNext(q1_block);
                        LeanVk::BeginCommand(cmd_b_q1);

                        Wait(q1_pile, SignalSemaphores[image_writen.TargetSemaphore].Handle, image_writen.Value);
                        Signal(q1_pile, SignalSemaphores[final_ticket.TargetSemaphore].Handle, final_ticket.Value);

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

                        LeanVk::EndCommand(cmd_b_q1);
                        AddCommandToPile(q1_pile, cmd_b_q1);
                        EndSubmission(q1_pile);
                    }
                    break;
                default:
                    assert(false && "unreachable path has been hit");
                    break;
            }
            StagingBuffer.Read(ring_buffer_read_size);
        }
    }

    void LazySubmit() {
        LazyWrite();

        /*
        analog::info("Prepared to submit\n\n\n");
        analog::info("Transfer Pile:");
        analog::info("{}", TransferSubmissionPile);
        analog::info("Graphics Pile:");
        analog::info("{}", SpecialSubmissionPiles[&VkVault::Graphics]);
        */

        for (auto* queue : VkVault::UniqueQueues) {
            SubmitPile(*queue, SpecialSubmissionPiles[queue], VK_NULL_HANDLE);
        }
        SubmitPile(VkVault::Transfer, TransferSubmissionPile, VK_NULL_HANDLE);

        WaitOn(TopTicket); // To safely wipe all command buffers, lazy sollution

        // TODO:CRITICAL: I have no idea on how I should track commands to reset for now
        // Reset(TransferCommandBufferBlock);

        /**
         * Step the layers only if the lazy write ended because of the current layer being emptied
         * it could be that the submission pile got full before that.
         */
        if (PackageQueues[CurrentUploadLayer].empty()){
            CurrentUploadLayer = (CurrentUploadLayer + 1) % UPLOAD_LAYERS_COUNT;
        }

        if (!ArePackageQueuesFullyEmptied()) {
            LazySubmit();
        }
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
        PackageQueues[CurrentUploadLayer].push(
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
        PackageQueues[CurrentUploadLayer].push(
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

    Ticket QueueImageSliceUpload(Image::Id dst, u32 target_layer, const void* src, u64 size, TransferType type) {
        assert(type == TransferType::Normal && "transfer type yet unsupported");

        u64 read_offset = StagingBuffer.Write(src, size);

        Ticket image_released_ticket = MakeTicket();
        Ticket image_writen_ticket = MakeTicket();
        Ticket image_acquired_ticket = MakeTicket();

        u32 layer = CurrentUploadLayer;
        u32 layer_plus = (CurrentUploadLayer + 1) % UPLOAD_LAYERS_COUNT;
        u32 layer_plus_plus = (CurrentUploadLayer + 2) % UPLOAD_LAYERS_COUNT;

        PackageQueues[layer].push(
            {
                .Type = PackageType::OwnerRelease,
                .Size = size,               // unused
                .TicketToSignal = image_released_ticket,
                .Data = {
                    .OwnerRelease = {
                        .ImageReleased = image_released_ticket,
                        .TargetImage = dst,
                        .TargetLayer = target_layer
                    }
                }
            }
        );

        PackageQueues[layer_plus].push(
            {
                .Type = PackageType::TransferAcquireWriteRelease,
                .Size = size,
                .TicketToSignal = image_writen_ticket,
                .Data = {
                    .TransferAcquireWriteRelease = {
                        .ImageReleased = image_released_ticket,
                        .ImageWriten = image_writen_ticket,
                        .DstImage = dst,
                        .TargetLayer = target_layer,
                        .CopyOffset = read_offset
                    }
                }
            }
        );

        PackageQueues[layer_plus_plus].push(
            {
                .Type = PackageType::OwnerAcquire,
                .Size = size,               // unused
                .TicketToSignal = image_acquired_ticket,
                .Data = {
                    .OwnerAcquire = {
                        .ImageWriten = image_writen_ticket,
                        .TargetImage = dst,
                        .TargetLayer = target_layer
                    }
                }
            }
        );

        return image_acquired_ticket;
    }
}
