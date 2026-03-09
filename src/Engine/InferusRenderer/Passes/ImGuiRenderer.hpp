#pragma once

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

class InferusRenderer;

#include "Engine/Types.hpp"

namespace ImGuiRenderer {
    InferusResult Create(InferusRenderer& InferusRenderer);
    void Destroy();

    void Render(VkCommandBuffer cmd);
};
