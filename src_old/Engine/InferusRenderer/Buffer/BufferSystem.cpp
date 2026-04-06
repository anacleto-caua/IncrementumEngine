#include "BufferSystem.hpp"

#include <cassert>

#include <spdlog/spdlog.h>

#include "Utils/ResourcePool.hpp"
#include "Engine/InferusRenderer/VulkanContext.hpp"
#include "Engine/InferusRenderer/RendererConfig.hpp"
#include "Engine/InferusRenderer/Buffer/BufferCreateOptions.hpp"

namespace BufferSystem {

    ResourcePool<Buffer> BufferPool;

    void clear(Buffer* buffer);
    void* map(VmaAllocation alloc);
    void unmap(VmaAllocation alloc);

    void Create() {
        BufferPool.Reserve(RendererConfig::BufferSystem::RESERVE_CAPACITY);
    }

    void Destroy() {
        for (Buffer& buffer : BufferPool) {
            clear(&buffer);
        }
        BufferPool.Clear();
    }

    Id add(CreateInfo createDesc) {
        Buffer buffer{};

        BufferCreateOptions::BufferOptions options = BufferCreateOptions::GetBufferOptions(createDesc.memType, createDesc.usage);

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = static_cast<VkDeviceSize>(createDesc.size);
        bufferCreateInfo.usage = options.vkUsage;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = options.vmaUsage;
        allocCreateInfo.requiredFlags = options.requiredFlags;
        allocCreateInfo.flags = options.vmaFlags;

        if (vmaCreateBuffer(VulkanContext::VmaAllocator, &bufferCreateInfo, &allocCreateInfo, &buffer.buffer, &buffer.allocation, nullptr) != VK_SUCCESS) {
            spdlog::error("Buffer creation failed");
        }

        buffer.size = createDesc.size;

        return BufferPool.Add(buffer);
    }

    Buffer* get(Id id) {
        Buffer* buffer = BufferPool.Get(id);
        assert(buffer != nullptr && "Attempted to access stale or invalid Buffer handle!");
        return buffer;
    }

    void del(Id id) {
        Buffer* buffer = get(id);
        if (buffer) {
            clear(buffer);
            BufferPool.Remove(id);
        }
    }

    void copy(VkCommandBuffer &cmd, Id srcId, Id dstId, const size_t size) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, get(srcId)->buffer, get(dstId)->buffer, 1, &copyRegion);
    }

    void copy(Id srcId, Id dstId, const size_t size) {
        vmaCopyMemoryToAllocation(VulkanContext::VmaAllocator, map(get(srcId)->allocation), get(dstId)->allocation, 0, size);
    }

    void upload(Id dstId, void* upload_Data) {
        upload(dstId, upload_Data, get(dstId)->size);
    }

    void upload(VkCommandBuffer &cmd, Id stagingId, Id dstId, const void* upload_Data, const size_t size) {
        upload(stagingId, upload_Data, size);
        copy(cmd, stagingId, dstId, size);
        return;
    }

    void upload(Id dstId, const void* upload_Data, const size_t size) {
        VmaAllocation alloc = get(dstId)->allocation;
        memcpy(map(alloc), upload_Data, size);
        unmap(alloc);
    }

    void* map(Id id) {
        return map(get(id)->allocation);
    }

    void unmap(Id id) {
        unmap(get(id)->allocation);
    }

    void* map(const VmaAllocation alloc) {
        void* mappedData;
        auto result = vmaMapMemory(VulkanContext::VmaAllocator, alloc, &mappedData);
        assert(result == VK_SUCCESS && "Failed to map VMA memory");
        return mappedData;
    }

    void unmap(const VmaAllocation alloc) {
        vmaUnmapMemory(VulkanContext::VmaAllocator, alloc);
    }

    void clear(Buffer* buffer) {
        if (buffer->buffer) { vmaDestroyBuffer(VulkanContext::VmaAllocator, buffer->buffer, buffer->allocation); }
        buffer->buffer = VK_NULL_HANDLE;
        buffer->allocation = VK_NULL_HANDLE;
    }
}
