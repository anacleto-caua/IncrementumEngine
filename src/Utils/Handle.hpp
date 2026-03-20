#pragma once
#include <cstdint>

template <typename T>
struct Handle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool IsValid() const {
        return index != UINT32_MAX;
    }

    bool operator==(const Handle<T>& other) const {
        return index == other.index && generation == other.generation;
    }
    bool operator!=(const Handle<T>& other) const {
        return !(*this == other);
    }
};
