#include "BinarySemaphore.hpp"

#include "Renderer/VkVault.hpp"

BinarySemaphore CreateBinarySemaphore() {
    BinarySemaphore semaphore;

    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.flags = 0;

    VK_OUT(
        vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &semaphore.Semaphore),
        "couldn't create a binary semaphore"
    );

    return semaphore;
}

void DestroyBinarySemaphore(BinarySemaphore& semaphore) {
    if (semaphore.Semaphore) { vkDestroySemaphore(VkVault::Device, semaphore.Semaphore, nullptr); }
    semaphore.Semaphore = VK_NULL_HANDLE;
}


