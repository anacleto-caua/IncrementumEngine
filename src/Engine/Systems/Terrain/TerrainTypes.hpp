#pragma once

#include <cstdint>
#include <type_traits>

#include "glm/ext/vector_int2.hpp"

enum class ChunkFlags : uint32_t {
    None  = 0,
    Active = 1 << 0,
    Visible = 1 << 1
};

inline constexpr ChunkFlags operator|(ChunkFlags a, ChunkFlags b) {
    using T = std::underlying_type_t<ChunkFlags>;
    return static_cast<ChunkFlags>(static_cast<T>(a) | static_cast<T>(b));
}

inline constexpr ChunkFlags operator&(ChunkFlags a, ChunkFlags b) {
    using T = std::underlying_type_t<ChunkFlags>;
    return static_cast<ChunkFlags>(static_cast<T>(a) & static_cast<T>(b));
}

struct ChunkHeightmapLink {
    glm::ivec2 WorldPos;
    uint32_t InstanceId;
    ChunkFlags Flags;
};
