#pragma once

#include <vector>
#include <cassert>

namespace asl {

template <typename T>
class FreeList {
private:
    struct Slot {
        T Data;
        bool Active;
    };

    std::vector<Slot> Data;
    std::vector<size_t> FreeIndices;

public:
    FreeList() = default;
    ~FreeList() = default;

    FreeList(const FreeList&) = delete;
    FreeList& operator=(const FreeList&) = delete;

    T& operator[](size_t index) {
        return Data[index].Data;
    }

    size_t Add(T element) {
        size_t index;
        if (FreeIndices.size() > 0) {
            index = FreeIndices[FreeIndices.size()-1];
            FreeIndices.pop_back();
            Data[index] = {
                .Data = element,
                .Active = true
            };
        } else {
            index = Data.size();
            Data.push_back({
                .Data = element,
                .Active = true
            });
        }
        return index;
    }

    void Remove(size_t index) {
        assert(Data[index].Active == true && "tried to remove an already invalid index.");

        Data[index].Active = false;
        FreeIndices.push_back(index);
    }

    class Iterator {
        private:
            std::vector<Slot>& DataPool;
            size_t Index;

            void SkipInactive() {
                while (Index < DataPool.size() && !DataPool[Index].Active) {
                    Index++;
                }
            }

        public:
            Iterator(std::vector<Slot>& data, size_t start_index) : DataPool(data), Index(start_index) {
                SkipInactive();
            }

            Iterator& operator++() {
                Index++;
                SkipInactive();
                return *this;
            }

            bool operator!=(const Iterator& other) const {
                return Index != other.Index;
            }

            T& operator*() {
                return DataPool[Index].Data;
            }
    };

    Iterator begin() {
        return Iterator(Data, 0);
    }

    Iterator end() {
        return Iterator(Data, Data.size());
    }

};

}
