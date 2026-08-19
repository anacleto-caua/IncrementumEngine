#pragma once

#include <array>
#include <cassert>

#include <imgui.h>

#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/BinarySemaphore.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"

template <
    QueueRole ROLE,
    u64 MAX_SUBMITS = 32,
    u64 MAX_COMMAND_BUFFERS = 64,
    u64 MAX_WAIT_SEMAPHORES = 64,
    u64 MAX_SIGNAL_SEMAPHORES = 64
>
struct SubmissionPile {
    static constexpr QueueRole Role = ROLE;
    static constexpr u64 MaxSubmits = MAX_SUBMITS;
    static constexpr u64 MaxCommandBuffers = MAX_COMMAND_BUFFERS;
    static constexpr u64 MaxWaitSemaphores = MAX_WAIT_SEMAPHORES;
    static constexpr u64 MaxSignalSemaphores = MAX_SIGNAL_SEMAPHORES;

    std::array<VkSubmitInfo2, MAX_SUBMITS> Submits = {{}};
    std::array<VkCommandBufferSubmitInfo, MAX_COMMAND_BUFFERS> CommandBuffers = {{}};
    std::array<VkSemaphoreSubmitInfo, MAX_WAIT_SEMAPHORES> WaitSemaphores = {{}};
    std::array<VkSemaphoreSubmitInfo, MAX_SIGNAL_SEMAPHORES> SignalSemaphores = {{}};

    u64 SubmitCount = 0;
    u64 CmdCount = 0;
    u64 WaitCount = 0;
    u64 SignalCount = 0;

    u64 CmdStart = 0;
    u64 WaitStart = 0;
    u64 SignalStart = 0;

    /*
     * It's cool having this but it breaks my special submission vector in TransferPipe
    Submission) = default;
    Submissionconst Submission) = delete;
    Submission operator=(const Submission) = delete;
    SubmissionSubmission&) = delete;
    Submission operator=(Submission&) = delete;
    */

    void Reset() {
        SubmitCount = CmdCount = WaitCount = SignalCount = 0;
        CmdStart = WaitStart = SignalStart = 0;
    }


    void BeginSubmission() {
        CmdStart = CmdCount;
        WaitStart = WaitCount;
        SignalStart = SignalCount;
    }


    void EndSubmission() {
        u64 command_quantity = CmdCount - CmdStart;
        u64 wait_semaphores_quantity = WaitCount - WaitStart;
        u64 signal_semaphores_quantity = SignalCount - SignalStart;

        assert(SubmitCount < MaxSubmits && "max submission count reached on a pile");

        Submits[SubmitCount] = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0,
            static_cast<u32>(wait_semaphores_quantity), wait_semaphores_quantity > 0 ? &WaitSemaphores[WaitStart] : nullptr,
            static_cast<u32>(command_quantity), command_quantity > 0 ? &CommandBuffers[CmdStart] : nullptr,
            static_cast<u32>(signal_semaphores_quantity), signal_semaphores_quantity > 0 ? &SignalSemaphores[SignalStart] : nullptr
        };
            SubmitCount++;
    }


    void AddCommand(VkCommandBuffer command) {
        assert(CmdCount < MaxCommandBuffers && "max command count reached on a pile");

        CommandBuffers[CmdCount] = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr,
            command, 0
        };
        CmdCount++;
    }

    // Timeline Semaphores

    void WaitSemaphore(const VkSemaphore semaphore, const u64 value, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        assert(WaitCount < MaxWaitSemaphores && "max wait semaphores on a pile reached");
        WaitSemaphores[WaitCount] = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
            semaphore, value, stage, 0
        };
        WaitCount++;
    }


    void SignalSemaphore(const VkSemaphore semaphore, const u64 value, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        assert(SignalCount < MaxSignalSemaphores && "max signal semaphores count on a pile reached");
        SignalSemaphores[SignalCount] = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
            semaphore, value, stage, 0
        };
        SignalCount++;
    }


    void WaitTimeline(const TimelineSemaphore& semaphore, const u64 value, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        WaitSemaphore( semaphore.Semaphore, value, stage);
    }


    void SignalTimeline(const TimelineSemaphore& semaphore, const u64 value, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        SignalSemaphore( semaphore.Semaphore, value, stage);
    }

    // Timeline Semaphores Ticket

    void WaitPrepareForTicket(const Ticket ticket, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        WaitTimeline( *ticket.TargetSemaphore, ticket.Value-1, stage);
    }


    void WaitForTicket(const Ticket ticket, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        WaitTimeline( *ticket.TargetSemaphore, ticket.Value, stage);
    }


    void SignalTicket(const Ticket ticket, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        SignalTimeline( *ticket.TargetSemaphore, ticket.Value, stage);
    }


    void WaitAndSignalTicket(const Ticket ticket, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        WaitPrepareForTicket( ticket, stage); // Guarantee all previously required work has been done
        SignalTicket( ticket, stage);
    }

    // Binary Semaphores

    void WaitBinarySemaphore(const BinarySemaphore& semaphore, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        assert(WaitCount < MaxWaitSemaphores && "max wait semaphores on a pile reached");
        WaitSemaphores[WaitCount] = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
            semaphore.Semaphore, 1, stage, 0
        };
        WaitCount++;
    }


    void SignalBinarySemaphore(const BinarySemaphore& semaphore, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE) {
        assert(SignalCount < MaxSignalSemaphores && "max signal semaphores count on a pile reached");
        SignalSemaphores[SignalCount] = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
            semaphore.Semaphore, 0, stage, 0
        };
        SignalCount++;
    }

    // Submission

    void Submit(VkFence execution_fence = VK_NULL_HANDLE) {
        if(SubmitCount > 0) {
            VK_OUT(vkQueueSubmit2(VkVault::Queues[ROLE].Queue, static_cast<u32>(SubmitCount), Submits.data(), execution_fence), "pile submission failed");
            Reset();
        }
    }

    // Utils

    bool IsFull() {
        return (
            SubmitCount == MaxSubmits ||
            CmdCount == MaxCommandBuffers ||
            WaitCount == MaxWaitSemaphores ||
            SignalCount == MaxSignalSemaphores
        );
    }


    bool IsEmpty() {
        return (
            SubmitCount == 0 &&
            CmdCount == 0 &&
            WaitCount == 0 &&
            SignalCount == 0
        );
    }

    // dear imgui fancy print - not test btw
    void ImGuiSubmission() {
        // Draw the high-level summary table
        if (ImGui::BeginTable("Submissionummary", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Resource");
            ImGui::TableSetupColumn("Usage / Max");
            ImGui::TableSetupColumn("Batch Start");
            ImGui::TableHeadersRow();

            // Submits
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Submits");
            ImGui::TableNextColumn(); ImGui::Text("%llu / %llu", (u64)SubmitCount, (u64)MaxSubmits);
            ImGui::TableNextColumn(); ImGui::Text("-");

            // Command Buffers
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Command Buffers");
            ImGui::TableNextColumn(); ImGui::Text("%llu / %llu", (u64)CmdCount, (u64)MaxCommandBuffers);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (u64)CmdStart);

            // Wait Semaphores
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Wait Semaphores");
            ImGui::TableNextColumn(); ImGui::Text("%llu / %llu", (u64)WaitCount, (u64)MaxWaitSemaphores);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (u64)WaitStart);

            // Signal Semaphores
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Signal Semaphores");
            ImGui::TableNextColumn(); ImGui::Text("%llu / %llu", (u64)SignalCount, (u64)MaxSignalSemaphores);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (u64)SignalStart);

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Text("Status: Empty? %s   |   Full? %s", IsEmpty() ? "Yes" : "No", IsFull() ? "Yes" : "No");
        ImGui::Separator();
        ImGui::Spacing();

        // Draw the interactive Topology Tree
        if (SubmitCount == 0) {
            ImGui::TextDisabled("[No Submits Recorded]");
            return;
        }

        ImGui::Text("=== SUBMISSION TOPOLOGY ===");

        for (u32 i = 0; i < SubmitCount; ++i) {
            const auto& submit = Submits[i];

            // utils for showing semaphores
            auto bullet_text_semaphore_out = [](const VkSemaphoreSubmitInfo& semaphore_submit_info){
                ImGui::BulletText("Semaphore: %p | Val: %llu | Stage: 0x%llx",
                  (void*)semaphore_submit_info.semaphore,
                  (u64)semaphore_submit_info.value,
                  (u64)semaphore_submit_info.stageMask);
            };

            // Ensure unique ID for ImGui tree nodes by using the loop index
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode((void*)(intptr_t)i, "Submit [%u]", i)) {

                ImGui::BulletText("Commands: %u", submit.commandBufferInfoCount);

                // Collapsible Waits Node
                if (ImGui::TreeNode((void*)(intptr_t)(i + 10000), "Waits: %u", submit.waitSemaphoreInfoCount)) {
                    for (u32 w = 0; w < submit.waitSemaphoreInfoCount; ++w) {
                        bullet_text_semaphore_out(submit.pWaitSemaphoreInfos[w]);
                    }
                    ImGui::TreePop();
                }

                // Collapsible Signals Node
                if (ImGui::TreeNode((void*)(intptr_t)(i + 20000), "Signals: %u", submit.signalSemaphoreInfoCount)) {
                    for (u32 s = 0; s < submit.signalSemaphoreInfoCount; ++s) {
                        bullet_text_semaphore_out(submit.pSignalSemaphoreInfos[s]);
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }
    }
};

// Fancy print

template <QueueRole R, u64 A, u64 B, u64 C, u64 D>
struct fmt::formatter<SubmissionPile<R, A, B, C, D>> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const SubmissionPile<R, A, B, C, D>& pile, FormatContext& ctx) const -> decltype(ctx.out()) {
        fmt::format_to(ctx.out(),
            "+------------------------------------------------+\n"
            "|         Detailed SubmissionState          |\n"
            "+---------------+-----------------+--------------+\n"
            "| Resource      | Usage / Max     | Batch Start  |\n"
            "+---------------+-----------------+--------------+\n"
            "| Submits       | {:>5} / {:<5} |      -       |\n"
            "| Command Buffs | {:>5} / {:<5} | {:>10}   |\n"
            "| Wait Semas    | {:>5} / {:<5} | {:>10}   |\n"
            "| Signal Semas  | {:>5} / {:<5} | {:>10}   |\n"
            "+---------------+-----------------+--------------+\n"
            "| Status:  Empty? {:<3}   Full? {:<3}              |\n"
            "+------------------------------------------------+\n",
            pile.SubmitCount, pile.MaxSubmits,
            pile.CmdCount, pile.MaxCommandBuffers, pile.CmdStart,
            pile.WaitCount, pile.MaxWaitSemaphores, pile.WaitStart,
            pile.SignalCount, pile.MaxSignalSemaphores, pile.SignalStart,
            pile.IsEmpty() ? "Yes" : "No",
            pile.IsFull() ? "Yes" : "No"
        );

        if (pile.SubmitCount == 0) {
            return fmt::format_to(ctx.out(), "\n  [No Submits Recorded]\n");
        }

        fmt::format_to(ctx.out(), "\n=== SUBMISSION TOPOLOGY ===\n");

        for (u32 i = 0; i < pile.SubmitCount; ++i) {
            const auto& submit = pile.Submits[i];
            fmt::format_to(ctx.out(), "v Submit [{}]\n", i);
            fmt::format_to(ctx.out(), "  |- Commands: {}\n", submit.commandBufferInfoCount);

            auto semaphore_out = [&ctx](const VkSemaphoreSubmitInfo& semaphore_submit_info){
                fmt::format_to(ctx.out(), "  |  |- Semaphore: {:p} | Val: {:<4} | Stage: {:#x}\n",
                   (void*)semaphore_submit_info.semaphore, semaphore_submit_info.value, semaphore_submit_info.stageMask);
            };

            fmt::format_to(ctx.out(), "  |- Waits: {}\n", submit.waitSemaphoreInfoCount);
            for (u32 w = 0; w < submit.waitSemaphoreInfoCount; ++w) {
                semaphore_out(submit.pWaitSemaphoreInfos[w]);
            }

            fmt::format_to(ctx.out(), "  \\- Signals: {}\n", submit.signalSemaphoreInfoCount);
            for (u32 s = 0; s < submit.signalSemaphoreInfoCount; ++s) {
                semaphore_out(submit.pSignalSemaphoreInfos[s]);
            }
            fmt::format_to(ctx.out(), "\n");
        }

        return ctx.out();
    }
};

