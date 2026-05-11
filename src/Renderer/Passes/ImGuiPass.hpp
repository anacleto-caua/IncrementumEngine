#pragma once

#include <imgui_impl_sdl3.h>

#include "Renderer/Vk.hpp"

namespace ImGuiPass {
    IncResult Create();

    void Destroy();

    void Render(VkCommandBuffer cmd);

    void ProcessEvent(SDL_Event event);
};
