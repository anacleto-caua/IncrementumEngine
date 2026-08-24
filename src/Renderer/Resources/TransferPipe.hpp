#pragma once

#include <queue>

#include "Image.hpp"
#include "Buffer.hpp"
#include "RingBuffer.hpp"
#include "ImageOwnershipTracker.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

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
     * from OwnershipTracker's own command buffer blocks, not the caller's). Keeps writing while
     * "pile" still has room - it's the caller's job to submit "pile" afterwards, so this can be
     * folded into an already-existing submission (e.g. a frame's own draw submit) instead of
     * paying for a dedicated vkQueueSubmit2. Forwards onto OwnershipTracker with Transfer fixed
     * as the acting queue and fragment-shader-read as the consumer, since that's the only pairing
     * TransferPipe itself needs.
     */
    template <typename PileT>
    void AcquirePending(QueueRole queue, PileT& pile) {
        OwnershipTracker.AcquirePending(
            queue, QueueRole::Transfer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
            pile
        );
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

    std::array<TimelineSemaphore, PARALLEL_TRANSFERS_COUNT> SignalSemaphores;
    u32 CurrentSemaphore = 0;

    std::queue<Package> PackageQueue;

    StandardSubmissionPile TransferSubmissionPile;
    CommandBufferBlock TransferCommandBufferBlock;

    ReclaimTracker TransferBlockReclaim; // TransferCommandBufferBlock (Transfer queue)

    RingBuffer<STAGING_BUFFER_SIZE> StagingBuffer;

    // Owns every piece of the owner<->Transfer image ownership round trip: release/acquire
    // barriers, their own command buffers/submission piles, and the AwaitingRelease/
    // AwaitingAcquire/PendingImageTransfersTimelineStatus bookkeeping. TransferPipe is one
    // instance's owning system; ComputePipe owns a separate instance for its own acting-queue
    // pairing.
    ImageOwnershipTracker<SPECIAL_PILE_SUBMITS, SPECIAL_PILE_COMMAND_BUFFERS, SPECIAL_PILE_WAIT_SEMAPHORES, SPECIAL_PILE_SIGNAL_SEMAPHORES> OwnershipTracker;

    Ticket MakeTicket();

    // Just write all packages, I need a version of this that controls how much it writes
    void LazyWrite();
};
