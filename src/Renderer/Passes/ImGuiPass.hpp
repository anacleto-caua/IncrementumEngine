#pragma once

#include <imgui_impl_sdl3.h>

#include "Pass.hpp"

class ImGuiPass : public Pass {
public:
    IncResult Init() override;
    void Destroy() override;
    void Render() override;

    void ProcessEvent(SDL_Event event);

private:
    void NewFrame();
};
