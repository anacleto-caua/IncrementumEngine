#pragma once

#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <type_traits>

namespace asl {

template <typename T>
class DArray {
private:
    static_assert(std::is_trivially_copyable<T>::value, "DArray supports only plain old data, no complex data types.");
public:
    T* Data = nullptr;
    size_t Size = 0;
    size_t Capacity = 0;

public:
    DArray() = default;
    ~DArray() {
        free(Data);
    }

    DArray(const DArray&) = delete;
    DArray& operator=(const DArray&) = delete;

    T& operator[](size_t index) {
        assert(index < Size && "index was out of bounds.");
        return Data[index];
    }

    void Push(T element) {
        if (Size == Capacity) {
            Capacity = (Capacity == 0) ? 1 : Capacity * 2;
            Data = (T*)realloc(Data, Capacity * sizeof(T));
            assert(Data != nullptr && "allocation failed.");
        }

        Data[Size] = element;
        Size++;
    }

    void Pop() {
        assert(Size > 0 && "can't pop an empty DArray.");
        Size--;
    }

    void Pack() {
        if (Size == 0) {
            free(Data);
            Data = nullptr;
            Capacity = 0;
        } else {
            Data = (T*)realloc(Data, Size * sizeof(T));
            Capacity = Size;
        }
    }

    void Reserve(size_t new_size) {
        assert(new_size >= Size && "new_size should be bigger than previous size.");
        Data = (T*)realloc(Data, new_size * sizeof(T));
        Capacity = new_size;
    }

    void Clear() {
        Size = 0;
    }

    // Defining standard iterator types for standard library compatibility
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() {
        return Data;
    }

    iterator end() {
        return Data + Size;
    }

    // Const versions (for when the DArray itself is const). lol.
    const_iterator begin() const {
        return Data;
    }

    const_iterator end() const {
        return Data + Size;
    }
};

}
