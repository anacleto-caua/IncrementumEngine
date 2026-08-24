#include "ComputePipe.hpp"

#include "Renderer/Vk/LeanVk.hpp"

IncResult ComputePipe::Init() {
    for (auto& semaphore : SignalSemaphores) {
        INC_CHECK(semaphore.Init(), "compute pipe signal semaphore creation failed");
    }

    ComputeSubmissionPileInstance.Reset();
    INC_CHECK(ComputeCommandBufferBlock.Init(QueueRole::Compute), "compute command buffer block creation failed");

    INC_CHECK(LazySemaphore.Init(), "compute pipe lazy semaphore creation failed");

    INC_CHECK(OwnershipTracker.Init(), "compute pipe ownership tracker creation failed");

    return IncResult::SUCCESS;
}

void ComputePipe::Destroy() {
    for (auto& semaphore : SignalSemaphores) {
        semaphore.Destroy();
    }
    LazySemaphore.Destroy();

    ComputeCommandBufferBlock.Destroy();

    OwnershipTracker.Destroy();
}

Ticket ComputePipe::MakeTicket() {
    Ticket ticket = SignalSemaphores[CurrentSemaphore].CreateTicket();
    CurrentSemaphore = (CurrentSemaphore + 1) % PARALLEL_COMPUTE_JOBS_COUNT;
    return ticket;
}

Ticket ComputePipe::QueueDispatch(const ComputeDispatchDesc& desc) {
    auto ticket = MakeTicket();
    PackageQueue.push({ .Desc = desc, .TicketToSignal = ticket });
    return ticket;
}

void ComputePipe::LazyWrite() {
    while (!PackageQueue.empty() && !ComputeSubmissionPileInstance.IsFull()) {
        Package package = PackageQueue.front();
        const ComputeDispatchDesc& desc = package.Desc;

        ComputeSubmissionPileInstance.BeginSubmission();
        VkCommandBuffer cmd = ComputeCommandBufferBlock.GetNext();
        LeanVk::BeginCommand(cmd);

        // Guarantee submission order (on this one semaphore) and make the ticket valid
        ComputeSubmissionPileInstance.WaitAndSignalTicket(package.TicketToSignal);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, desc.Pipeline);

        if (desc.DescriptorSetCount > 0) {
            vkCmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_COMPUTE, desc.Layout,
                0, desc.DescriptorSetCount, desc.DescriptorSets.data(),
                0, nullptr
            );
        }

        if (desc.PushConstantSize > 0) {
            vkCmdPushConstants(
                cmd, desc.Layout, desc.PushConstantStages,
                0, desc.PushConstantSize, desc.PushConstantData.data()
            );
        }

        vkCmdDispatch(cmd, desc.GroupCountX, desc.GroupCountY, desc.GroupCountZ);

        LeanVk::EndCommand(cmd);
        ComputeSubmissionPileInstance.AddCommand(cmd);
        ComputeSubmissionPileInstance.EndSubmission();

        ComputeBlockReclaim = { package.TicketToSignal, true };
        PackageQueue.pop();
    }
}

void ComputePipe::SubmitQueued() {
    LazyWrite();
    ComputeSubmissionPileInstance.Submit(VK_NULL_HANDLE);
}

void ComputePipe::LazySubmit() {
    SubmitQueued();

    vkQueueWaitIdle(VkVault::Queues[QueueRole::Compute].Queue);

    ComputeCommandBufferBlock.Reset();
    ComputeBlockReclaim.Valid = false;

    // Step again only if the write above ended because the pile got full, not because the
    // queue was actually drained - same recursion shape as TransferPipe::LazySubmit.
    if (!PackageQueue.empty()) {
        LazySubmit();
    }
}

void ComputePipe::TryReclaimCommandBuffers() {
    if (ComputeBlockReclaim.Valid && ComputeBlockReclaim.LastTicket.IsFinished()) {
        ComputeCommandBufferBlock.Reset();
        ComputeBlockReclaim.Valid = false;
    }

    OwnershipTracker.TryReclaimCommandBuffers();
}
