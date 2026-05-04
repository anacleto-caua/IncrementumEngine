#pragma once

#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <type_traits>

namespace asl {

template <typename T>
class darray {
private:
    static_assert(std::is_trivially_copyable<T>::value, "darray supports only plain old data, no complex data types.");
public:
    T* Data = nullptr;
    size_t Count = 0;
    size_t Capacity = 0;

private:
public:
    darray() = default;
    ~darray() {
        free(Data);
    }

    darray(const darray&) = delete;
    darray& operator=(const darray&) = delete;

    T& operator[](size_t index) {
        assert(index < Count && "index was out of bounds.");
        return Data[index];
    }

    void push(T element) {
        if (Count == Capacity) {
            Count++;
            Capacity++;
            Data = (T*)realloc(Data, Capacity * sizeof(T));
        } else {
            Data[Count] = element;
            Count++;
        }
    }

    void pop() {
        assert(Count > 0 && "can't pop an empty darray.");
        Count--;
    }

    void pack() {
        if (Count == 0) {
            free(Data);
            Data = nullptr;
            Capacity = 0;
        } else {
            Data = (T*)realloc(Data, Count * sizeof(T));
            Capacity = Count;
        }
    }

    void reserve(size_t new_size) {
        assert(new_size >= Count && "new_size should be bigger than previous size.");
        Data = (T*)realloc(Data, new_size * sizeof(T));
        Capacity = new_size;
    }
};

}
