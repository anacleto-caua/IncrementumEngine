#include "SubmissionPile.hpp"

#include <array>

constexpr u64 MAX_SUBMITS = 32;
constexpr u64 MAX_COMMAND_BUFFERS = 64;
constexpr u64 MAX_SEMAPHORES = 64;

struct SubmissionPile {
    std::array<VkSubmitInfo2, MAX_SUBMITS> Submits;
    std::array<VkCommandBufferSubmitInfo, MAX_COMMAND_BUFFERS> CommandBuffers;
    std::array<VkSemaphoreSubmitInfo, MAX_SEMAPHORES> WaitSemaphores;
    std::array<VkSemaphoreSubmitInfo, MAX_SEMAPHORES> SignalSemaphores;

    u64 SubmitCount = 0;
    u64 CmdCount = 0;
    u64 WaitCount = 0;
    u64 SignalCount = 0;

    u64 CmdStart = 0;
    u64 WaitStart = 0;
    u64 SignalStart = 0;
};

void Reset(SubmissionPile& pile) {
    pile.SubmitCount = pile.CmdCount = pile.WaitCount = pile.SignalCount = 0;
    pile.CmdStart = pile.WaitStart = pile.SignalStart = 0;
}

SubmissionPile& Begin(SubmissionPile& pile) {
    pile.CmdStart = pile.CmdCount;
    pile.WaitStart = pile.WaitCount;
    pile.SignalStart = pile.SignalCount;
    return pile;
}

SubmissionPile& Command(SubmissionPile& pile, VkCommandBuffer command) {
    pile.CommandBuffers[pile.CmdCount++] = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr,
        command, 0
    };
    return pile;
}

SubmissionPile& Wait(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage) {
    pile.WaitSemaphores[pile.WaitCount++] = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
        semaphore, value, stage, 0
    };
    return pile;
}

SubmissionPile& Signal(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage) {
    pile.SignalSemaphores[pile.SignalCount++] = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
        semaphore, value, stage, 0
    };
    return pile;
}

void End(SubmissionPile& pile) {
    u64 command_quantity = pile.CmdCount - pile.CmdStart;
    u64 wait_semaphores_quantity = pile.WaitCount - pile.WaitStart;
    u64 signal_semaphores_quantity = pile.SignalCount - pile.SignalStart;

    pile.Submits[pile.SubmitCount++] = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0,
        static_cast<u32>(wait_semaphores_quantity), wait_semaphores_quantity > 0 ? &pile.WaitSemaphores[pile.WaitStart] : nullptr,
        static_cast<u32>(command_quantity), command_quantity > 0 ? &pile.CommandBuffers[pile.CmdStart] : nullptr,
        static_cast<u32>(signal_semaphores_quantity), signal_semaphores_quantity > 0 ? &pile.SignalSemaphores[pile.SignalStart] : nullptr
    };
}

void SubmitHarvestedPiles(QueueContext& ctx, VkFence execution_fence, SubmissionPile* piles, u64 pile_count) {
    std::array<VkSubmitInfo2, 128> global_submits;
    u64 total_submits = 0;

    for (u64 i = 0; i < pile_count; i++) {
        SubmissionPile& pile = piles[i];

        for (u64 j = 0; j < pile.SubmitCount; j++) {
            global_submits[total_submits++] = pile.Submits[j];
        }

        Reset(pile);
    }

    if (total_submits > 0) {
        vkQueueSubmit2(ctx.Queue, static_cast<u32>(total_submits), global_submits.data(), execution_fence);
    }
}
