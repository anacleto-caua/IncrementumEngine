#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

struct BinarySemaphore {
    VkSemaphore Semaphore = VK_NULL_HANDLE;
};

BinarySemaphore CreateBinarySemaphore();
void DestroyBinarySemaphore(BinarySemaphore& semaphore);

