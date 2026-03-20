#pragma once

#include <vector>
#include <cassert>

#include "Utils/Handle.hpp"

template <typename T>
class ResourcePool {
private:

    struct Slot {
        T data;
        uint32_t generation = 0;
        bool active = false;
    };

    std::vector<Slot> data;
    std::vector<uint32_t> freeIndices;

    static constexpr double DATA_TO_FREE_IDS_RESERVE_RATIO = (1.0/10.0);

public:
    void Reserve(size_t capacity) {
        data.reserve(capacity);
        freeIndices.reserve(capacity*DATA_TO_FREE_IDS_RESERVE_RATIO);
    }

    Handle<T> Add(const T& item) {
        uint32_t index;
        uint32_t currentGeneration;

        if (!freeIndices.empty()) {
            // Reuse an old slot
            index = freeIndices.back();
            freeIndices.pop_back();

            currentGeneration = data[index].generation;
            data[index].data = item;
            data[index].active = true;
        } else {
            // Create a brand new slot
            index = static_cast<uint32_t>(data.size());
            currentGeneration = 0;

            data.push_back({item, currentGeneration, true});
        }

        return Handle<T>{index, currentGeneration};
    }

    T* Get(Handle<T> handle) {
        if (!handle.IsValid() || handle.index >= data.size()) {
            return nullptr;
        }

        Slot& slot = data[handle.index];

        if (!slot.active || slot.generation != handle.generation) {
            return nullptr;
        }

        return &slot.data;
    }

    void Remove(Handle<T> handle) {
        if (!handle.IsValid() || handle.index >= data.size()) return;

        Slot& slot = data[handle.index];

        // Only remove if it's currently active and matches the generation
        if (slot.active && slot.generation == handle.generation) {
            slot.active = false;
            slot.generation++;
            freeIndices.push_back(handle.index);
        }
    }

    void Clear() {
        data.clear();
        freeIndices.clear();
    }

    class Iterator {
        private:
            std::vector<Slot>& poolData;
            size_t index;

            void SkipInactive() {
                while (index < poolData.size() && !poolData[index].active) {
                    index++;
                }
            }

        public:
            Iterator(std::vector<Slot>& data, size_t startIndex) : poolData(data), index(startIndex) {
                SkipInactive();
            }

            Iterator& operator++() {
                index++;
                SkipInactive();
                return *this;
            }

            bool operator!=(const Iterator& other) const {
                return index != other.index;
            }

            T& operator*() {
                return poolData[index].data;
            }
    };

    Iterator begin() {
        return Iterator(data, 0);
    }

    Iterator end() {
        return Iterator(data, data.size());
    }
};
