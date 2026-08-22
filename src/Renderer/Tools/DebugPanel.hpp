#pragma once

#include <imgui.h>

// Shared top bar coordinating every pass's ImGui debug section (TerrainPass::OutTerrainData(),
// PropPass::OutPropData(), SkyPass::OutSkyData(), TextPass::OutTextData(), ImGuiPass's own
// "Renderer" section). A real ImGui main menu bar - pinned to the very top of the viewport, no
// title bar, no collapse button, always full width - rather than a regular collapsible window,
// matching the toolbar most software has at the top. Each item independently opens/closes its own
// section's window (not mutually exclusive - any number can be open together, same as any other
// set of independent tool windows). Renderer::Frame() calls DrawToolbar() once per frame, before
// any pass renders; each pass then wraps its own content in BeginSection(Section)/EndSection() to
// draw into its section's own window only while that section is open.
namespace DebugPanel {
    enum class Section : u32 {
        Terrain = 0,
        Props,
        Sky,
        Text,
        Renderer,
        _COUNT_
    };

    inline constexpr const char* SectionNames[] = { "Terrain", "Props", "Sky", "Text", "Renderer" };
    inline bool SectionOpen[static_cast<u32>(Section::_COUNT_)] = {};

    inline void DrawToolbar() {
        if (ImGui::BeginMainMenuBar()) {
            for (u32 i = 0; i < static_cast<u32>(Section::_COUNT_); i++) {
                ImGui::MenuItem(SectionNames[i], nullptr, &SectionOpen[i]);
            }
            ImGui::EndMainMenuBar();
        }
    }

    // Returns false (drawing nothing further, no matching EndSection() needed) unless this
    // section's window is open - mirrors the bare-Begin()-return-gates-content ImGui idiom.
    inline bool BeginSection(Section section) {
        u32 index = static_cast<u32>(section);
        if (!SectionOpen[index]) { return false; }

        // Seeds new windows just below the menu bar on first use - after that, wherever the user
        // last moved it. Passing &SectionOpen[index] gives the window its own native close (X)
        // button too, staying in sync with the menu bar checkbox either way it's toggled.
        ImGui::SetNextWindowPos(ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiCond_FirstUseEver);
        ImGui::Begin(SectionNames[index], &SectionOpen[index]);
        return true;
    }

    inline void EndSection() {
        ImGui::End();
    }
}
