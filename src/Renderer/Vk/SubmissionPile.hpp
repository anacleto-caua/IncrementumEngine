#pragma once

#include "Renderer/VkVault.hpp"

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

void Reset(SubmissionPile& pile);

void Begin(SubmissionPile& pile);
void End(SubmissionPile& pile);

void Command(SubmissionPile& pile, VkCommandBuffer command);

void Wait(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0);
void Signal(SubmissionPile& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0);

void Wait(SubmissionPile& pile, const TimelineSemaphore& semaphore, VkPipelineStageFlags2 stage = 0);
void Signal(SubmissionPile& pile, TimelineSemaphore& semaphore, VkPipelineStageFlags2 stage = 0);

void SubmitPile(QueueContext& ctx, SubmissionPile& pile, VkFence execution_fence = VK_NULL_HANDLE);
void SubmitMultiplePiles(QueueContext& ctx, SubmissionPile* piles, u64 pile_count, VkFence execution_fence = VK_NULL_HANDLE);
