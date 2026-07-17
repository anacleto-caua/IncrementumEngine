#include "pch.hpp"

#include <spdlog/spdlog.h>

namespace analog {
    void init() {
        spdlog::set_pattern("%^[%l]%$ %v");
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::critical);
        spdlog::flush_on(spdlog::level::info);
    }
}
