#include "Vk.hpp"

#include "Renderer/VkVault.hpp"

TimelineSemaphore CreateTimelineSemaphore() {
    TimelineSemaphore semaphore = {};
    semaphore.LastValue = 0;

    VkSemaphoreTypeCreateInfo semaphore_type_create_info = {};
    semaphore_type_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphore_type_create_info.pNext = nullptr;
    semaphore_type_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphore_type_create_info.initialValue = 0;

    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.pNext = &semaphore_type_create_info;
    semaphore_create_info.flags = 0;

    vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &semaphore.Handle);

    return semaphore;
}

void DestroyTimelineSemaphore(TimelineSemaphore& semaphore) {
    if (semaphore.Handle) { vkDestroySemaphore(VkVault::Device, semaphore.Handle, nullptr); }
}
