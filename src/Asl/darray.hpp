#pragma once

#include <cstddef>

namespace asl{
template <typename T>
class darray {
private:
    T* Head = nullptr;
    size_t Count;
    size_t Capacity;

public:
    void push(T element) {
        if (Count == Capacity) {
            Count++;
            Capacity++;
            Head = realloc(Head, Capacity);
        } else {
            Head[Count] = element;
            Count++;
        }
    }
};

}
