#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

struct TimelineSemaphoreValue {
    u64 LastSignaledValue = 0;
    u64 LastInqueriedValue = 0;
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
