#pragma once

#include <array>

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

    void Reset() {
        SubmitCount = CmdCount = WaitCount = SignalCount = 0;
        CmdStart = WaitStart = SignalStart = 0;
    }

    SubmissionPile& Begin() {
        CmdStart = CmdCount;
        WaitStart = WaitCount;
        SignalStart = SignalCount;
        return *this;
    }

    SubmissionPile& Command(VkCommandBuffer command) {
        CommandBuffers[CmdCount++] = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr,
            command, 0
        };
        return *this;
    }

    SubmissionPile& Wait(VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0) {
        WaitSemaphores[WaitCount++] = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
            semaphore, value, stage, 0
        };
        return *this;
    }

    SubmissionPile& Signal(VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0) {
        SignalSemaphores[SignalCount++] = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
            semaphore, value, stage, 0
        };
        return *this;
    }

    void End() {
        u64 command_quantity = CmdCount - CmdStart;
        u64 wait_semaphores_quantity = WaitCount - WaitStart;
        u64 signal_semaphores_quantity = SignalCount - SignalStart;

        Submits[SubmitCount++] = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0,
            static_cast<u32>(wait_semaphores_quantity), wait_semaphores_quantity > 0 ? &WaitSemaphores[WaitStart] : nullptr,
            static_cast<u32>(command_quantity), command_quantity > 0 ? &CommandBuffers[CmdStart] : nullptr,
            static_cast<u32>(signal_semaphores_quantity), signal_semaphores_quantity > 0 ? &SignalSemaphores[SignalStart] : nullptr
        };
    }
};

void SubmitHarvestedPiles(QueueContext& ctx, VkFence execution_fence, SubmissionPile* piles, u64 pile_count);
