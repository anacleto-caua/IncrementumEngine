#pragma once

#include <array>
#include <string>
#include <cassert>

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

// Damn I sure love having to repeat myself a lot to use generics :)

template <u64 A, u64 B, u64 C, u64 D>
void Reset(SubmissionPile<A, B, C, D>& pile) {
    pile.SubmitCount = pile.CmdCount = pile.WaitCount = pile.SignalCount = 0;
    pile.CmdStart = pile.WaitStart = pile.SignalStart = 0;
}

template <u64 A, u64 B, u64 C, u64 D>
void Begin(SubmissionPile<A, B, C, D>& pile) {
    pile.CmdStart = pile.CmdCount;
    pile.WaitStart = pile.WaitCount;
    pile.SignalStart = pile.SignalCount;
}

template <u64 A, u64 B, u64 C, u64 D>
void End(SubmissionPile<A, B, C, D>& pile) {
    u64 command_quantity = pile.CmdCount - pile.CmdStart;
    u64 wait_semaphores_quantity = pile.WaitCount - pile.WaitStart;
    u64 signal_semaphores_quantity = pile.SignalCount - pile.SignalStart;

    assert(pile.SubmitCount < pile.MaxSubmits && "max submission count reached on a pile");

    pile.Submits[pile.SubmitCount] = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0,
        static_cast<u32>(wait_semaphores_quantity), wait_semaphores_quantity > 0 ? &pile.WaitSemaphores[pile.WaitStart] : nullptr,
        static_cast<u32>(command_quantity), command_quantity > 0 ? &pile.CommandBuffers[pile.CmdStart] : nullptr,
        static_cast<u32>(signal_semaphores_quantity), signal_semaphores_quantity > 0 ? &pile.SignalSemaphores[pile.SignalStart] : nullptr
    };
    pile.SubmitCount++;
}

template <u64 A, u64 B, u64 C, u64 D>
void Command(SubmissionPile<A, B, C, D>& pile, VkCommandBuffer command) {
    assert(pile.CmdCount < pile.MaxCommandBuffers && "max command count reached on a pile");

    pile.CommandBuffers[pile.CmdCount] = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr,
        command, 0
    };
    pile.CmdCount++;
}

template <u64 A, u64 B, u64 C, u64 D>
void Wait(SubmissionPile<A, B, C, D>& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0) {
    assert(pile.WaitCount < pile.MaxWaitSemaphores && "max wait semaphores on a pile reached");

    pile.WaitSemaphores[pile.WaitCount] = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
        semaphore, value, stage, 0
    };
    pile.WaitCount++;
}

template <u64 A, u64 B, u64 C, u64 D>
void Signal(SubmissionPile<A, B, C, D>& pile, VkSemaphore semaphore, u64 value, VkPipelineStageFlags2 stage = 0) {
    assert(pile.SignalCount < pile.MaxSignalSemaphores && "max signal semaphores count on a pile reached");

    pile.SignalSemaphores[pile.SignalCount] = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
        semaphore, value, stage, 0
    };
    pile.SignalCount++;
}

template <u64 A, u64 B, u64 C, u64 D>
void Wait(SubmissionPile<A, B, C, D>& pile, const TimelineSemaphore& semaphore, VkPipelineStageFlags2 stage = 0) {
    Wait(pile, semaphore.Handle, semaphore.LastSignaledValue, stage);
}

template <u64 A, u64 B, u64 C, u64 D>
void Signal(SubmissionPile<A, B, C, D>& pile, TimelineSemaphore& semaphore, VkPipelineStageFlags2 stage = 0) {
    semaphore.LastSignaledValue += 1;
    Signal(pile, semaphore.Handle, semaphore.LastSignaledValue, stage);
}

template <u64 A, u64 B, u64 C, u64 D>
bool IsFull(SubmissionPile<A, B, C, D> pile) {
    return (
        pile.SubmitCount == pile.MaxSubmits ||
        pile.CmdCount == pile.MaxCommandBuffers ||
        pile.WaitCount == pile.MaxWaitSemaphores ||
        pile.SignalCount == pile.MaxSignalSemaphores
    );
}

template <u64 A, u64 B, u64 C, u64 D>
bool IsEmpty(SubmissionPile<A, B, C, D> pile) {
    return (
        pile.SubmitCount == 0 &&
        pile.CmdCount == 0 &&
        pile.WaitCount == 0 &&
        pile.SignalCount == 0
    );
}

template <u64 A, u64 B, u64 C, u64 D>
void SubmitPile(QueueContext& ctx, SubmissionPile<A, B, C, D>& pile, VkFence execution_fence = VK_NULL_HANDLE) {
    if(pile.SubmitCount >= 1) {
        VK_OUT(vkQueueSubmit2(ctx.Queue, static_cast<u32>(pile.SubmitCount), pile.Submits.data(), execution_fence), "pile submission failed");
        Reset(pile);
    }
}

/*
template <u64 A, u64 B, u64 C, u64 D>
std::string FancyOutSubmissionPile(const SubmissionPile<A, B, C, D>& pile) {
    std::string out = fmt::format(
        "╭────────────────────────────────────────────────╮\n"
        "│         Detailed SubmissionPile State          │\n"
        "├───────────────┬─────────────────┬──────────────┤\n"
        "│ Resource      │ Usage / Max     │ Batch Start  │\n"
        "├───────────────┼─────────────────┼──────────────┤\n"
        "│ Submits       │ {:>5} / {:<5} │      -       │\n"
        "│ Command Buffs │ {:>5} / {:<5} │ {:>10}   │\n"
        "│ Wait Semas    │ {:>5} / {:<5} │ {:>10}   │\n"
        "│ Signal Semas  │ {:>5} / {:<5} │ {:>10}   │\n"
        "├───────────────┴─────────────────┴──────────────┤\n"
        "│ Status:  Empty? {:<3}   Full? {:<3}              │\n"
        "╰────────────────────────────────────────────────╯\n",
        pile.SubmitCount, pile.MaxSubmits,
        pile.CmdCount, pile.MaxCommandBuffers, pile.CmdStart,
        pile.WaitCount, pile.MaxWaitSemaphores, pile.WaitStart,
        pile.SignalCount, pile.MaxSignalSemaphores, pile.SignalStart,
        IsEmpty(pile) ? "Yes" : "No",
        IsFull(pile) ? "Yes" : "No"
    );

    if (pile.SubmitCount == 0) {
        out += "\n  [No Submits Recorded]\n";
        return out;
    }

    out += "\n=== SUBMISSION TOPOLOGY ===\n";

    for (u32 i = 0; i < pile.SubmitCount; ++i) {
        const auto& submit = pile.Submits[i];
        out += fmt::format("▼ Submit [{}]\n", i);
        out += fmt::format("  ├─ Commands: {}\n", submit.commandBufferInfoCount);

        out += fmt::format("  ├─ Waits: {}\n", submit.waitSemaphoreInfoCount);
        for (u32 w = 0; w < submit.waitSemaphoreInfoCount; ++w) {
            const auto& waitInfo = submit.pWaitSemaphoreInfos[w];
            // Cast to void* so fmt prints the raw Vulkan handle safely
            out += fmt::format("  │  ├─ Sema: {:p} | Val: {:<4} | Stage: {:#x}\n",
                               (void*)waitInfo.semaphore, waitInfo.value, waitInfo.stageMask);
        }

        out += fmt::format("  └─ Signals: {}\n", submit.signalSemaphoreInfoCount);
        for (u32 s = 0; s < submit.signalSemaphoreInfoCount; ++s) {
            const auto& sigInfo = submit.pSignalSemaphoreInfos[s];
            out += fmt::format("     ├─ Sema: {:p} | Val: {:<4} | Stage: {:#x}\n",
                               (void*)sigInfo.semaphore, sigInfo.value, sigInfo.stageMask);
        }
        out += "\n";
    }

    return out;
}

*/
