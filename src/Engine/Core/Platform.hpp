#pragma once

#include <functional>

/**
 * Responsible for Window and Input.
 */

using ResizeCallback = std::function<void(i32 width, i32 height)>;

namespace Platform {
    IncResult Initialize(i32 width, i32 height, const std::string title, ResizeCallback callback);

    void Update();

    void Shutdown();

    bool ShouldClose();

    // Makes ShouldClose() return true from the next check on, same as the user closing the window.
    void RequestClose();
}
