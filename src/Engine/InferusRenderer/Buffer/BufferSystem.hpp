#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "Utils/Handle.hpp"

namespace BufferSystem {
    enum class CreateInfoMemoryType {
        // STRICT GPU-ONLY.
        GPU_STATIC,
        // CPU WRITES -> GPU READS.
        CPU_TO_GPU,
        // CPU WRITES -> GPU COPIES -> GPU READS.
        STAGING_UPLOAD,
        // GPU WRITES -> CPU READS.
        READBACK,

        _BUFFER_MEMORY_TYPE_COUNT_
    };

    enum class CreateInfoUsage {
        VERTEX,
        INDEX,
        SSBO,
        UBO,
        STAGING,

        _BUFFER_USAGE_COUNT_
    };

    struct Buffer {
        VkBuffer buffer;
        VmaAllocation allocation;
        size_t size;
        CreateInfoMemoryType memType;
        CreateInfoUsage usage;
    };

    struct CreateInfo {
        size_t size = 0;
        CreateInfoMemoryType memType;
        CreateInfoUsage usage;
    };

    using Id = Handle<Buffer>;

    void Create();
    void Destroy();

    Id add(CreateInfo createDesc);
    void del(Id id);

    Buffer* get(Id id);

    void copy(Id srcId, Id dstId, const size_t size);
    void copy(VkCommandBuffer &cmd, Id srcId, Id dstId, const size_t size);

    void upload(Id dstId, void* upload_data);
    void upload(Id dstId, const void* upload_data, const size_t size);
    void upload(VkCommandBuffer &cmd, Id stagingId, Id dstId, const void* upload_data, const size_t size);

    void* map(Id id);
    void unmap(Id id);
};
