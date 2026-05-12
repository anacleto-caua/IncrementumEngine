#pragma once

#include "Renderer/Vk.hpp"
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

    template <typename T, u32 COUNT>
    struct Mirror {
        Id Device;
        Id Host;
        T* Data;

        T* begin() {
            return Data;
        }

        T* end() {
            return Data + COUNT;
        }

        const T* begin() const {
            return Data;
        }

        const T* end() const {
            return Data + COUNT;
        }
    };
}
