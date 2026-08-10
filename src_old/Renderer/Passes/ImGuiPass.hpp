#pragma once

#include <imgui_impl_sdl3.h>

namespace ImGuiPass {
    IncResult Create();

    void Destroy();

    void Render();

    void ProcessEvent(SDL_Event event);
};
