#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

struct TimelineSemaphoreValue {
    u64 LastPromissedValue = 0;  // The last value the semaphore will eventually be
    u64 LastInqueriedValue = 0; // The last value the semaphore was
    VkSemaphore Semaphore = VK_NULL_HANDLE;
};

struct TimelineSemaphore {
    u16 Index;
};

TimelineSemaphore CreateTimelineSemaphore();
void DestroyTimelineSemaphore(TimelineSemaphore semaphore);

TimelineSemaphoreValue* GetTimelineSemaphoreValue(TimelineSemaphore id);

void QueryTimelineSemaphoreValue(TimelineSemaphore semaphore);
void SignalTimelineSemaphore(TimelineSemaphore semaphore, u64 signal_value);
void WaitOnTimelineSemaphore(TimelineSemaphore semaphore, u64 wait_value);

struct Ticket {
    u64 Value;
    TimelineSemaphore TargetSemaphore;
};

Ticket CreateTicket(TimelineSemaphore semaphore);

bool IsFinished(Ticket ticket);
void WaitOn(Ticket ticket);
