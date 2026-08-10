#pragma once

// core types
#include <cstdint>

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

// --- core macros
#define INC_CHECK(expr, ...)                            \
    do {                                                \
        if ((expr) != IncResult::SUCCESS) {             \
            analog::critical(__VA_ARGS__);              \
            return IncResult::FAIL;                     \
        }                                               \
    } while(0)

enum class IncResult {
    SUCCESS,
    FAIL
};

// --- logging
#include <spdlog/spdlog.h>

namespace analog {
    /**
     * Make sure to call this function at the start of the program
     */
    inline void init() {
        spdlog::set_pattern("%^[%l]%$ %v");
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::err);
    }
    // Debug level
    template <typename... Args>
    inline void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::debug(fmt, std::forward<Args>(args)...);
    }

    // Info level
    template <typename... Args>
    inline void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    // Warn level
    template <typename... Args>
    inline void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    // Error level
    template <typename... Args>
    inline void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }

    // Critical level
    template <typename... Args>
    inline void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::critical(fmt, std::forward<Args>(args)...);
    }

}
