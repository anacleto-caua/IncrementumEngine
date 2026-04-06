#pragma once
#include <cstdint>

template <typename T>
struct Handle {
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    bool IsValid() const {
        return Index != UINT32_MAX;
    }

    bool operator==(const Handle<T>& other) const {
        return Index == other.Index && Generation == other.Generation;
    }
    bool operator!=(const Handle<T>& other) const {
        return !(*this == other);
    }
};
