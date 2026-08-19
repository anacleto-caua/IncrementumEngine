#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "Renderer/VkVault.hpp"
#include "Core/ResourcePool.hpp"

struct Buffer {
    enum class Type {
        STAGING,
        VERTEX,
        INDEX,
        SSBO,
        UBO,

        _COUNT_
    };

    struct CreateInfo {
        size_t Size;
        Type Type;
    };

    VkBuffer Handle = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    size_t Size = 0;
    Type BufferType = Type::_COUNT_;
};

using BufferId = Handle<Buffer>;

struct BufferPool {
    IncResult Add(Buffer::CreateInfo create_info, BufferId& out_id) {
        Buffer buffer {};

        VkBufferUsageFlags vk_usage = 0;
        VmaMemoryUsage vma_usage = VMA_MEMORY_USAGE_UNKNOWN;
        VmaAllocationCreateFlags vma_flags = 0;
        VkMemoryPropertyFlags memory_flags = 0;

        switch (create_info.Type) {
            case Buffer::Type::STAGING:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = 0;
                break;
            case Buffer::Type::VERTEX:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case Buffer::Type::INDEX:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case Buffer::Type::SSBO:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case Buffer::Type::UBO:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            default:
                analog::error("unexpected type on buffer creation.");
                return IncResult::FAIL;
        }

        VkBufferCreateInfo buffer_create_info {};
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size = static_cast<VkDeviceSize>(create_info.Size);
        buffer_create_info.usage = vk_usage;

        VmaAllocationCreateInfo alloc_create_info {};
        alloc_create_info.usage = vma_usage;
        alloc_create_info.requiredFlags = memory_flags;
        alloc_create_info.flags = vma_flags;

        VK_CHECK(
            vmaCreateBuffer(
                VkVault::VmaAllocator,
                &buffer_create_info, &alloc_create_info,
                &buffer.Handle, &buffer.Allocation,
                nullptr
            ),
            "buffer creation failed."
        );

        buffer.Size = create_info.Size;
        buffer.BufferType = create_info.Type;

        out_id = Pool.Add(buffer);
        return IncResult::SUCCESS;
    }

    void Del(BufferId id) {
        Buffer* buffer = Get(id);
        Destroy(buffer);
        Pool.Remove(id);
    }

    Buffer* Get(BufferId id) {
        return &Pool.Get(id);
    }

    void* Map(BufferId id) {
        void* mapped_data;
        vmaMapMemory(VkVault::VmaAllocator, Get(id)->Allocation, &mapped_data);
        return mapped_data;
    }

    void Unmap(BufferId id) {
        vmaUnmapMemory(VkVault::VmaAllocator, Get(id)->Allocation);
    }

    void DestroyAll() {
        for (auto buffer : Pool) {
            Destroy(&buffer);
        }
    }

private:
    ResourcePool<Buffer> Pool;

    void Destroy(Buffer* buffer) {
        if (buffer->Handle) { vmaDestroyBuffer(VkVault::VmaAllocator, buffer->Handle, buffer->Allocation); }
        buffer->Handle = VK_NULL_HANDLE;
        buffer->Allocation = VK_NULL_HANDLE;
    }
};

inline BufferPool Buffers;
