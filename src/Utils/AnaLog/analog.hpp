#pragma once

#include <format>
#include <iostream>
#include <string_view>

namespace analog {
    // Info level (Green text)
    template <typename... Args>
    inline void info(std::string_view fmt_str, Args&&... args) {
        std::cout << "\033[1;32m[INFO]\033[0m "
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
}
