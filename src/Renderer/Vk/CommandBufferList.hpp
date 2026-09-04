#pragma once

#include <array>

#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/LeanVk.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"

template <QueueRole Role, u64 Count>
struct CommandBufferList {
    VkCommandPool Pool = VK_NULL_HANDLE;

    // Parallel arrays, each semaphore tracks a single buffer
    std::array<TimelineSemaphore, Count> Semaphores = {{ VK_NULL_HANDLE }};
    std::array<VkCommandBuffer, Count> Buffers = {{ VK_NULL_HANDLE }};

    u32 Head = 0; // The next command buffer to be used

    CommandBufferList() = default;
    ~CommandBufferList() { Destroy(); }
    CommandBufferList(const CommandBufferList&) = delete;
    CommandBufferList& operator=(const CommandBufferList&) = delete;
    CommandBufferList(CommandBufferList&&) noexcept = default;
    CommandBufferList& operator=(CommandBufferList&&) noexcept = default;

    IncResult Init() {
        VkCommandPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = VkVault::Queues[Role].FamilyIndex;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.pNext = nullptr;

        VK_CHECK(vkCreateCommandPool(VkVault::Device, &pool_info, nullptr, &Pool), "couldn't create a command pool");

        VkCommandBufferAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = Pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = Count;
        alloc_info.pNext = nullptr;

        VK_CHECK(vkAllocateCommandBuffers(VkVault::Device, &alloc_info, Buffers.data()));

        for (TimelineSemaphore& binary_semaphore : Semaphores) {
            INC_CHECK(binary_semaphore.Init());
        }

        Head = 0;

        return IncResult::SUCCESS;
    }

    void Destroy() {
        for (TimelineSemaphore& binary_semaphore : Semaphores) {
            binary_semaphore.Destroy();
        }

        if (Pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(VkVault::Device, Pool, nullptr);
        }

        Pool = VK_NULL_HANDLE;
        Buffers = {{ VK_NULL_HANDLE }};
        Semaphores = {{ VK_NULL_HANDLE }};
    }

    void ResetPool() {
        for (u32 i = 0; i < Head; i++) {
            Semaphores[i].Wait(Semaphores[i].LastPromissedValue);
        }

        vkResetCommandPool(VkVault::Device, Pool, 0);
        Head = 0;
    }

    void GetNext(VkCommandBuffer& command, Ticket& wait_and_signal) {
        Semaphores[Head].Query();
        if (Semaphores[Head].LastPromissedValue < Semaphores[Head].LastInquiredValue) {
            analog::warn("had to wait on a CommandBufferList to get a usefull command buffer, consider enlarging this one list");
            Semaphores[Head].Wait(Semaphores[Head].LastPromissedValue);
        }

        wait_and_signal = Semaphores[Head].CreateTicket();
        command = Buffers[Head];
        LeanVk::ResetCommand(command);

        Head = (Head + 1) % Count;
    }
};
