#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "Renderer/VkVault.hpp"

struct Ticket;

struct TimelineSemaphore {
    VkSemaphore Semaphore = VK_NULL_HANDLE;
    u64 LastPromissedValue = 0;  // The last value the semaphore will eventually be
    u64 LastInqueriedValue = 0; // The last value the semaphore was

    TimelineSemaphore() = default;

    IncResult Init() {
        LastPromissedValue = 0;
        LastInqueriedValue = 0;

        VkSemaphoreTypeCreateInfo semaphore_type_create_info = {};
        semaphore_type_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semaphore_type_create_info.pNext = nullptr;
        semaphore_type_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_type_create_info.initialValue = 0;

        VkSemaphoreCreateInfo semaphore_create_info = {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create_info.pNext = &semaphore_type_create_info;
        semaphore_create_info.flags = 0;

        VK_CHECK(
            vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &Semaphore),
            "couldn't create a timeline semaphore"
        );

        return IncResult::SUCCESS;
    }

    void Destroy() {
        if (Semaphore) { vkDestroySemaphore(VkVault::Device, Semaphore, nullptr); }
        Semaphore = VK_NULL_HANDLE;
    }

    ~TimelineSemaphore() { Destroy(); }

    TimelineSemaphore(const TimelineSemaphore&) = delete;
    TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
    // Not movable either - Ticket::TargetSemaphore holds a raw pointer to whichever
    // TimelineSemaphore created it, and every TimelineSemaphore in this codebase is a stable-
    // address member of a long-lived owner (Renderer::FrameSemaphore, TransferPipe::
    // LazySemaphore/SignalSemaphores - see Game/Game.hpp for how those are ultimately reached)
    // that's never relocated.
    TimelineSemaphore(TimelineSemaphore&&) = delete;
    TimelineSemaphore& operator=(TimelineSemaphore&&) = delete;

    void Query() {
        vkGetSemaphoreCounterValue(VkVault::Device, Semaphore, &LastInqueriedValue);
    }

    void Signal(u64 value) {
        VkSemaphoreSignalInfo signal_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .pNext = nullptr,
            .semaphore = Semaphore,
            .value = value
        };
        vkSignalSemaphore(VkVault::Device, &signal_info);

        LastInqueriedValue = value;
    }

    void Wait(u64 value) {
        VkSemaphoreWaitInfo wait_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = nullptr,
            .flags = 0,
            .semaphoreCount = 1,
            .pSemaphores = &Semaphore,
            .pValues = &value
        };

        vkWaitSemaphores(VkVault::Device, &wait_info, UINT64_MAX);
        LastInqueriedValue = value;
    }

    // Can't move this one in with the rest: it constructs a Ticket by value, and Ticket isn't a
    // complete type yet at this point (it's defined right below) - Ticket's own methods need
    // TimelineSemaphore complete too, so one side of this mutual reference has to stay out-of-line.
    Ticket CreateTicket();
};

struct Ticket {
    u64 Value = 0;
    TimelineSemaphore* TargetSemaphore = nullptr;

    bool IsFinished() const {
        if (TargetSemaphore->LastInqueriedValue < Value) {
            TargetSemaphore->Query();
        }
        return TargetSemaphore->LastInqueriedValue >= Value;
    }

    void WaitOn() const {
        TargetSemaphore->Wait(Value);
    }
};

inline Ticket TimelineSemaphore::CreateTicket() {
    return {
        .Value = ++LastPromissedValue,
        .TargetSemaphore = this
    };
}
