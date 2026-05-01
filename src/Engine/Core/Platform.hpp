#pragma once

#include <functional>

#include "Types.hpp"

/**
 * Responsible for Window and Input.
 */

using ResizeCallback = std::function<void(u32 width, u32 height)>;

namespace Platform {
    IncResult Initialize(u32 width, u32 height, const std::string title, ResizeCallback callback);

    void Update();

    void Shutdown();

    bool ShouldClose();
}
