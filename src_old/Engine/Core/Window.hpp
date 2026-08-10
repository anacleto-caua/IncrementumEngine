#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace Window {
    enum class WindowMode {
        Windowed,
        Borderless,
        Fullscreen
    };

    inline WindowMode CurrentMode = WindowMode::Windowed;

    std::vector<const char*> GetRequiredExtensions();
    bool CreateSurface(VkInstance instance, VkSurfaceKHR &surface);
    void GetFramebufferSize(i32 &width, i32 &height);

    void SetSize(u32 width, u32 height);
    void SetTitle(const char* title);

    void SetWindowMode(WindowMode mode);
    void ToggleFullscreen();

    void CenterPositionOnCurrentMonitor();
};
