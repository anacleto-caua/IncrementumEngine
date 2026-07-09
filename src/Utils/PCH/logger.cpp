#include "pch.hpp"

#include <spdlog/spdlog.h>

// implements pch methods that may be needed

namespace analog {
    void init() {
        // Now you can change this pattern without recompiling the whole engine!
        spdlog::set_pattern("%^[%l]%$ %v");
        spdlog::set_level(spdlog::level::debug);
    }
}
