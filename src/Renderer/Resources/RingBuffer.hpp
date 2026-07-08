#pragma once

#include <cstring>
#include <cassert>

#include "ResourceManager.hpp"

template <u64 SIZE>
class RingBuffer {
public:
    Buffer::Id Buffer;

private:
    static constexpr u64 ALIGNMENT = 4;

    u8* MappedHead;
    u8* Head;
    u8* Tail;

    static constexpr u64 Align(u64 value) {
        return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

public:
    void Create() {
        Buffer::CreateInfo CreateInfo = {
            .Size = SIZE,
            .Type = Buffer::Type::STAGING
        };

        Buffer = Buffer::Add(CreateInfo);
        MappedHead = static_cast<u8*>(Buffer::Map(Buffer::Get(Buffer)->Allocation));
        Head = MappedHead;
        Tail = Head;
    }

    void Destroy() {
        Buffer::Unmap(Buffer::Get(Buffer)->Allocation);
        Buffer::Del(Buffer);
    }

    u64 Write(const void* src, u64 upload_size) {
        u64 aligned_size = Align(upload_size);

        assert(aligned_size < SIZE && "single queued upload is bigger than staging buffer itself");

        u64 neck_size = static_cast<u64>((MappedHead + SIZE) - Head);
        u64 write_offset = static_cast<u64>(Head - MappedHead);

        if (upload_size <= neck_size) {
            memcpy(Head, src, upload_size);
            Head += aligned_size;

        } else {
            // Just wrap and begin writing to the head anyway, spliting into to 2 uploads is bad:
            // - Worse perf from non-contiguous memory
            // - Terribly messy with images and their different pixel sizes

            u64 pre_tail = static_cast<u64>(Tail - MappedHead);
            // This could be easy memcpy'ed to growing pool but I aim to catch bad usage for now,
            // the heftier sollution can come in later
            assert(pre_tail > aligned_size && "can't fit whole data from the head on, staging buffer is cluttered");

            write_offset = 0;
            memcpy(MappedHead, src, upload_size);
            Head = MappedHead + aligned_size;
        }

        return write_offset;
    }

    void Read(u64 package_size) {
        u64 aligned_package_size = Align(package_size);

        u64 tail_neck_size = static_cast<u64>((MappedHead + SIZE) - Tail);

        if (aligned_package_size  <= tail_neck_size) {
            Tail += aligned_package_size;
        } else {
            Tail = MappedHead + aligned_package_size;
        }
    }
};
