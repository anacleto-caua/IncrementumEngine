#include "Vk.hpp"

#include "Renderer/VkVault.hpp"
#include "vulkan/vulkan_core.h"

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


u64 QueryTimelineSemaphoreValue(TimelineSemaphore& semaphore) {
    u64 value;
    vkGetSemaphoreCounterValue(VkVault::Device, semaphore.Handle, &value);
    return value;
}

void SignalTimelineSemaphore(TimelineSemaphore& semaphore, u64 value) {
    VkSemaphoreSignalInfo signal_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .pNext = nullptr,
        .semaphore = semaphore.Handle,
        .value = value
    };
    vkSignalSemaphore(VkVault::Device, &signal_info);
}
