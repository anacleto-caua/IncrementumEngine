#include <array>
#include <cstdint>
#include <utility>
#include <cassert>

template <size_t SIZE>
class DataBin {
private:
    alignas(std::max_align_t) std::array<uint8_t, SIZE> Memory;
    size_t Head = 0;

public:
    template<typename T, typename... Args>
    T* Push(Args&&... args) {
        size_t alignment = alignof(T);
        size_t padding = (alignment - (Head % alignment)) % alignment;

        size_t aligned_head = Head + padding;
        assert(aligned_head + sizeof(T) <= SIZE && "FrameBin out of memory!");

        void* ptr = &Memory[aligned_head];
        Head = aligned_head + sizeof(T);

        return new (ptr) T(std::forward<Args>(args)...);
    }

    void Reset() {
        Head = 0;
    }
};
