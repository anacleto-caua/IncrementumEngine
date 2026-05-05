#pragma once

#include <format>
#include <iostream>
#include <string_view>

namespace analog {

    // Debug level (Cyan text)
    template <typename... Args>
    inline void debug(std::string_view fmt_str, Args&&... args) {
        std::cout << "\033[1;36m[DEBUG]\033[0m "
                  << std::vformat(fmt_str, std::make_format_args(args...))
                  << '\n';
    }

    // Info level (Green text)
    template <typename... Args>
    inline void info(std::string_view fmt_str, Args&&... args) {
        std::cout << "\033[1;32m[INFO]\033[0m "
                  << std::vformat(fmt_str, std::make_format_args(args...))
                  << '\n';
    }

    // Warn level (Yellow text)
    template <typename... Args>
    inline void warn(std::string_view fmt_str, Args&&... args) {
        std::cout << "\033[1;33m[WARN]\033[0m "
                  << std::vformat(fmt_str, std::make_format_args(args...))
                  << '\n';
    }

    // Error level (Red text)
    template <typename... Args>
    inline void error(std::string_view fmt_str, Args&&... args) {
        std::cerr << "\033[1;31m[ERROR]\033[0m "
                  << std::vformat(fmt_str, std::make_format_args(args...))
                  << '\n';
    }

    // Critical level (Magenta text)
    template <typename... Args>
    inline void critical(std::string_view fmt_str, Args&&... args) {
        std::cerr << "\033[1;35m[CRITICAL]\033[0m "
                  << std::vformat(fmt_str, std::make_format_args(args...))
                  << '\n';
    }

}
