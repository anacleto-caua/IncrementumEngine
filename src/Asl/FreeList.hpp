#pragma once

#include <vector>
#include <limits>
#include <cassert>

namespace asl {

template <typename T, typename IndexingType = size_t>
class FreeList {
private:
    struct Slot {
        T Data;
        bool Active;
    };

    std::vector<Slot> Data;
    std::vector<IndexingType> FreeIndices;

public:
    FreeList() = default;
    ~FreeList() = default;

    FreeList(const FreeList&) = delete;
    FreeList& operator=(const FreeList&) = delete;

    T& operator[](IndexingType index) {
        return Data[index].Data;
    }

    IndexingType Add(T element) {
        IndexingType index;
        if (FreeIndices.size() > 0) {
            index = FreeIndices[FreeIndices.size()-1];
            FreeIndices.pop_back();
            Data[index] = {
                .Data = element,
                .Active = true
            };
        } else {
            assert(Data.size() <= std::numeric_limits<IndexingType>::max() && "tried to add more elements than limited by the chosen IndexingType.");
            index = static_cast<IndexingType>(Data.size());

            Data.push_back({
                .Data = element,
                .Active = true
            });
        }
        return index;
    }

    void Remove(IndexingType index) {
        assert(Data[index].Active == true && "tried to remove an already invalid index.");

        Data[index].Active = false;
        FreeIndices.push_back(index);
    }

    class Iterator {
        private:
            std::vector<Slot>& DataPool;
            IndexingType Index;

            void SkipInactive() {
                while (Index < DataPool.size() && !DataPool[Index].Active) {
                    Index++;
                }
            }

        public:
            Iterator(std::vector<Slot>& data, IndexingType start_index) : DataPool(data), Index(start_index) {
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
