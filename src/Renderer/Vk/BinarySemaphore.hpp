#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "Renderer/VkVault.hpp"

struct BinarySemaphore {
    VkSemaphore Semaphore = VK_NULL_HANDLE;

    BinarySemaphore() = default;

    IncResult Init() {
        VkSemaphoreCreateInfo semaphore_create_info = {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create_info.flags = 0;

        VK_CHECK(
            vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &Semaphore),
            "couldn't create a binary semaphore"
        );

        return IncResult::SUCCESS;
    }

    void Destroy() {
        if (Semaphore) { vkDestroySemaphore(VkVault::Device, Semaphore, nullptr); }
        Semaphore = VK_NULL_HANDLE;
    }

    ~BinarySemaphore() { Destroy(); }

    // Not copyable (would double-destroy the same VkSemaphore); movable.
    BinarySemaphore(const BinarySemaphore&) = delete;
    BinarySemaphore& operator=(const BinarySemaphore&) = delete;
    BinarySemaphore(BinarySemaphore&& other) noexcept : Semaphore(other.Semaphore) { other.Semaphore = VK_NULL_HANDLE; }
    BinarySemaphore& operator=(BinarySemaphore&& other) noexcept {
        if (this != &other) { Destroy(); Semaphore = other.Semaphore; other.Semaphore = VK_NULL_HANDLE; }
        return *this;
    }
};
