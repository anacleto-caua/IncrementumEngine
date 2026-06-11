#include <imgui.h>

namespace ImGuiUtils {
    void OutFps(float delta_time) {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

        // Position: Bottom-Right
        const float pad = 10.0f;
        const ImVec2 viewport_pos = ImGui::GetMainViewport()->WorkPos;
        const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
        ImVec2 window_pos = { (viewport_pos.x + viewport_size.x - pad), (viewport_pos.y + viewport_size.y - pad) };
        ImVec2 window_pos_pivot = { 1.0f, 1.0f }; // Pivot on bottom-right corner

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

        if (ImGui::Begin("Perf Overlay", nullptr, window_flags)) {
            ImGui::Text("FPS: %.1f (%.3f ms)", 1.0f / delta_time, delta_time * 1000.0f);
        }
        ImGui::End();
    }
}
