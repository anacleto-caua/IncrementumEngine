#pragma once

#include <array>
#include <queue>

#include "ImageOwnershipTracker.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"

inline constexpr u32 COMPUTE_MAX_DESCRIPTOR_SETS = 4;
inline constexpr u32 COMPUTE_MAX_PUSH_CONSTANT_BYTES = 128; // Vulkan's guaranteed minimum maxPushConstantsSize

inline constexpr u64 COMPUTE_PILE_SUBMITS = 32;
inline constexpr u64 COMPUTE_PILE_COMMAND_BUFFERS = 32;
inline constexpr u64 COMPUTE_PILE_WAIT_SEMAPHORES = 64;
inline constexpr u64 COMPUTE_PILE_SIGNAL_SEMAPHORES = 32;

// Sized smaller than TransferPipe's x20 - compute dispatches aren't expected at terrain-streaming
// volumes yet. Bump these if a real workload overflows it, same class of capacity bug documented
// on TransferPipe's own SPECIAL_PILE_* constants. File-scope (not class-private) because
// ComputePipe::OwnershipTracker's type needs them before the class body that would otherwise
// define them is complete.
inline constexpr u64 COMPUTE_SPECIAL_PILE_SUBMITS = COMPUTE_PILE_SUBMITS * 4;
inline constexpr u64 COMPUTE_SPECIAL_PILE_COMMAND_BUFFERS = COMPUTE_PILE_COMMAND_BUFFERS * 4;
inline constexpr u64 COMPUTE_SPECIAL_PILE_WAIT_SEMAPHORES = COMPUTE_PILE_WAIT_SEMAPHORES * 4;
inline constexpr u64 COMPUTE_SPECIAL_PILE_SIGNAL_SEMAPHORES = COMPUTE_PILE_SIGNAL_SEMAPHORES * 4;

// Pure data - which pipeline, which descriptor sets, which push constants, which group counts -
// not behavior. Every named compute job fills one of these and calls ComputePipe::QueueDispatch();
// it owns its own VkPipeline/VkPipelineLayout/VkDescriptorSet(s) the same way TerrainPass owns its
// own graphics pipeline, without ComputePipe needing a "job type" hierarchy to know about it.
struct ComputeDispatchDesc {
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, COMPUTE_MAX_DESCRIPTOR_SETS> DescriptorSets = {};
    u32 DescriptorSetCount = 0;

    std::array<u8, COMPUTE_MAX_PUSH_CONSTANT_BYTES> PushConstantData = {};
    u32 PushConstantSize = 0; // 0 = no push constants bound
    VkShaderStageFlags PushConstantStages = VK_SHADER_STAGE_COMPUTE_BIT;

    u32 GroupCountX = 1;
    u32 GroupCountY = 1;
    u32 GroupCountZ = 1;
};

// Queues async vkCmdDispatch work the same way TransferPipe queues transfers: callers get back a
// Ticket (a timeline-semaphore wait point) instead of blocking on the dispatch. Not a Pass - a
// plain Renderer-owned service, reached ambiently via the GComputePipe alias in Game/Game.hpp,
// same shape as TransferPipe.
class ComputePipe {
public:
    IncResult Init();
    void Destroy();

    // Returns a Ticket the caller waits/polls on for completion - same type and by-value-return
    // idiom as TransferPipe::QueueBufferUpload/QueueImageSliceUpload/etc.
    Ticket QueueDispatch(const ComputeDispatchDesc& desc);

    // Flushes the entire package queue, blocking until the Compute queue is idle - the "queue and
    // wait" analog of TransferPipe::LazySubmit(), fine for one-shot startup-time dispatches.
    void LazySubmit();

    // Writes+submits whatever currently fits in the pile, without blocking and without resetting
    // any command buffers - the streaming-friendly analog of TransferPipe::SubmitReleaseAndWrite(),
    // for kicking off dispatches outside Renderer::Frame().
    void SubmitQueued();

    // Non-blocking, cheap to call every frame - reclaims the command buffer block once its last
    // submitted ticket has actually finished on the GPU.
    void TryReclaimCommandBuffers();

    // Same generalized ownership-transfer machinery TransferPipe uses (see
    // ImageOwnershipTracker.hpp), wired and ready for any future compute job that reads/writes an
    // image owned by another queue role - unused by buffer-only dispatches, since Buffer (unlike
    // Image) carries no OwnerQueue/ownership-transfer tracking anywhere in this codebase.
    ImageOwnershipTracker<
        COMPUTE_SPECIAL_PILE_SUBMITS, COMPUTE_SPECIAL_PILE_COMMAND_BUFFERS,
        COMPUTE_SPECIAL_PILE_WAIT_SEMAPHORES, COMPUTE_SPECIAL_PILE_SIGNAL_SEMAPHORES
    > OwnershipTracker;

private:
    static constexpr u64 PARALLEL_COMPUTE_JOBS_COUNT = 15; // mirrors TransferPipe::PARALLEL_TRANSFERS_COUNT

    using ComputeSubmissionPile = SubmissionPile<
            QueueRole::Compute,
            COMPUTE_PILE_SUBMITS,
            COMPUTE_PILE_COMMAND_BUFFERS,
            COMPUTE_PILE_WAIT_SEMAPHORES,
            COMPUTE_PILE_SIGNAL_SEMAPHORES
        >;

    struct Package {
        ComputeDispatchDesc Desc;
        Ticket TicketToSignal;
    };

    struct ReclaimTracker {
        Ticket LastTicket;
        bool Valid = false;
    };

    TimelineSemaphore LazySemaphore; // orders submissions on ComputeCommandBufferBlock - mirrors TransferPipe::LazySemaphore
    std::array<TimelineSemaphore, PARALLEL_COMPUTE_JOBS_COUNT> SignalSemaphores; // round-robin ticket pool for concurrent in-flight dispatches
    u32 CurrentSemaphore = 0;

    std::queue<Package> PackageQueue;

    ComputeSubmissionPile ComputeSubmissionPileInstance;
    CommandBufferBlock ComputeCommandBufferBlock;
    ReclaimTracker ComputeBlockReclaim;

    Ticket MakeTicket(); // identical shape to TransferPipe::MakeTicket
    void LazyWrite();    // drains PackageQueue: bind pipeline, bind sets, push constants, vkCmdDispatch
};
