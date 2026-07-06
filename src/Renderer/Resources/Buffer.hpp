#pragma once

#include "Renderer/VkVault.hpp"
#include "Asl/ResourcePool.hpp"

namespace Buffer {
    enum class Type {
        STAGING,
        VERTEX,
        INDEX,
        SSBO,
        UBO,

        _COUNT_
    };

    struct Value {
        VkBuffer Buffer;
        VmaAllocation Allocation;
        size_t Size;
        Type Type;
    };

    using Id = asl::Handle<Value>;
}
