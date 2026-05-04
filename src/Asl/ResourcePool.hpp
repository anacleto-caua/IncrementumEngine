#pragma once

#include <cstdint>

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
    DArray<uint32_t> Generations; // Parallel array tracking generations

public:
    Handle<T> Add(T element) {
        size_t index = Data.Add(element);

        if (index == Generations.Size) {
            Generations.Push(0);
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
        assert(handle.Generation == Generations[handle.Index] && "Tried to remove a stale handle.");
        Generations[handle.Index]++;
        Data.Remove(handle.Index);
    }};

}
