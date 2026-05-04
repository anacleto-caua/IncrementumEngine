#pragma once

#include "Asl/DArray.hpp"

namespace asl {

template <typename T>
class FreeList {
private:
    struct Slot {
        T Data;
        bool Active;
    };

    DArray<Slot> Data;
    DArray<size_t> FreeIndices;

public:
    FreeList() = default;
    ~FreeList() = default;

    FreeList(const FreeList&) = delete;
    FreeList& operator=(const FreeList&) = delete;

    T& operator[](size_t index) {
        return Data[index].Data;
    }

    size_t Add(T element) {
        size_t index = -1;
        if (FreeIndices.Size > 0) {
            index = FreeIndices[FreeIndices.Size-1];
            FreeIndices.Pop();
            Data[index] = {
                .Data = element,
                .Active = true
            };
        } else {
            index = Data.Size;
            Data.Push({
                .Data = element,
                .Active = true
            });
        }
        return index;
    }

    void Remove(size_t index) {
        assert(Data[index].Active == true && "tried to remove an already invalid index.");

        Data[index].Active = false;
        FreeIndices.Push(index);
    }

    class Iterator {
        private:
            DArray<Slot>& DataPool;
            size_t Index;

            void SkipInactive() {
                while (Index < DataPool.Size && !DataPool[Index].Active) {
                    Index++;
                }
            }

        public:
            Iterator(DArray<Slot>& data, size_t start_index) : DataPool(data), Index(start_index) {
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
        return Iterator(Data, Data.Size);
    }

};

}
