#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

struct TimelineSemaphore {
    u64 LastValue = 0;
    VkSemaphore Handle = VK_NULL_HANDLE;
};

TimelineSemaphore CreateTimelineSemaphore();
void DestroyTimelineSemaphore(TimelineSemaphore& semaphore);

u64 QueryTimelineSemaphoreValue(TimelineSemaphore& semaphore);
