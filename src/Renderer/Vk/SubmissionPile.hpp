#pragma once

#include "Renderer/VkVault.hpp"

template <
    u64 MAX_SUBMITS = 32,
    u64 MAX_COMMAND_BUFFERS = 64,
    u64 MAX_WAIT_SEMAPHORES = 64,
    u64 MAX_SIGNAL_SEMAPHORES = 64
>
struct SubmissionPile {
    static constexpr u64 MaxSubmits = MAX_SUBMITS;
    static constexpr u64 MaxCommandBuffers = MAX_COMMAND_BUFFERS;
    static constexpr u64 MaxWaitSemaphores = MAX_WAIT_SEMAPHORES;
    static constexpr u64 MaxSignalSemaphores = MAX_SIGNAL_SEMAPHORES;

    std::array<VkSubmitInfo2, MAX_SUBMITS> Submits;
    std::array<VkCommandBufferSubmitInfo, MAX_COMMAND_BUFFERS> CommandBuffers;
    std::array<VkSemaphoreSubmitInfo, MAX_WAIT_SEMAPHORES> WaitSemaphores;
    std::array<VkSemaphoreSubmitInfo, MAX_SIGNAL_SEMAPHORES> SignalSemaphores;

    u64 SubmitCount = 0;
    u64 CmdCount = 0;
    u64 WaitCount = 0;
    u64 SignalCount = 0;

    u64 CmdStart = 0;
    u64 WaitStart = 0;
    u64 SignalStart = 0;
};

void Reset(SubmissionPile<>& pile);

void Begin(SubmissionPile<>& pile);
void End(SubmissionPile<>& pile);

void Command(SubmissionPile<>& pile, VkCommandBuffer command);

void Wait(SubmissionPile<>& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0);
void Signal(SubmissionPile<>& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0);

void Wait(SubmissionPile<>& pile, const TimelineSemaphore& semaphore, VkPipelineStageFlags2 stage = 0);
void Signal(SubmissionPile<>& pile, TimelineSemaphore& semaphore, VkPipelineStageFlags2 stage = 0);

void SubmitPile(QueueContext& ctx, SubmissionPile<>& pile, VkFence execution_fence = VK_NULL_HANDLE);
void SubmitMultiplePiles(QueueContext& ctx, SubmissionPile<>* piles, u64 pile_count, VkFence execution_fence = VK_NULL_HANDLE);
