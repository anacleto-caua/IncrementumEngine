#pragma once

#include <cstdint>
#include <cassert>

#include "Asl/FreeList.hpp"

namespace asl {

template <typename T>
struct Handle {
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    // Hmmm...
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

template <typename T>
class ResourcePool {
private:
    FreeList<T> Data;
    std::vector<uint32_t> Generations; // Parallel array tracking generations

public:
    Handle<T> Add(T element) {
        u32 index = static_cast<u32>(Data.Add(element));

        if (index == Generations.size()) {
            Generations.push_back(0);
        }

        Handle<T> h;
        h.Index = index;
        h.Generation = Generations[index];
        return h;
    }

    T& Get(Handle<T> handle) {
        assert(handle.Generation == Generations[handle.Index] && "tried to access a stale handle.");
        return Data[handle.Index];
    }

    void Remove(Handle<T> handle) {
        assert(handle.Generation == Generations[handle.Index] && "tried to remove a stale handle.");
        Generations[handle.Index]++;
        Data.Remove(handle.Index);
    }

    using Iterator = typename FreeList<T>::Iterator;

    Iterator begin() {
        return Data.begin();
    }

    Iterator end() {
        return Data.end();
    }
};
}
