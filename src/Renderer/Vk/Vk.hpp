#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

struct TimelineSemaphore {
    u64 LastSignaledValue = 0;
    u64 LastInqueriedValue = 0;
    VkSemaphore Handle = VK_NULL_HANDLE;
};

TimelineSemaphore CreateTimelineSemaphore();
void DestroyTimelineSemaphore(TimelineSemaphore& semaphore);

void QueryTimelineSemaphoreValue(TimelineSemaphore& semaphore);
void SignalTimelineSemaphore(TimelineSemaphore& semaphore, u64 value);
void WaitOnTimelineSemaphore(TimelineSemaphore& semaphore, u64 wait_value);
