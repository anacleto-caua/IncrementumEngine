#include "PerfOverlay.hpp"

#include <algorithm>
#include <array>

#include <imgui.h>

#include "Engine/Core/ProcessMetrics.hpp"

namespace PerfOverlay {
    // Enough history for a readable graph and a stable 1% low at typical frame rates - a few
    // seconds' worth at 60fps.
    constexpr u32 HistorySize = 240;

    std::array<f32, HistorySize> FrameTimesMs{};
    u32 HistoryCount = 0;
    u32 HistoryCursor = 0;

    // Process metrics are polled on a timer rather than every frame: both platforms' queries are
    // syscalls, and the numbers are far too noisy frame-to-frame to read at 60Hz anyway.
    constexpr f32 MetricsPollInterval = 0.5f;
    f32 TimeSinceMetricsPoll = MetricsPollInterval;

    struct SortedHistory {
        std::array<f32, HistorySize> Values;
        u32 Count;
    };

    SortedHistory GetSortedHistory() {
        SortedHistory sorted;
        sorted.Count = HistoryCount;
        std::copy_n(FrameTimesMs.begin(), HistoryCount, sorted.Values.begin());
        std::sort(sorted.Values.begin(), sorted.Values.begin() + HistoryCount);
        return sorted;
    }

    // "1% low" here is the frame-time percentile convention: the mean of the slowest 1% of frames,
    // reported as the FPS that time corresponds to. It surfaces hitching that an average FPS hides.
    f32 PercentileLowFps(const SortedHistory& sorted, f32 percentile) {
        if (sorted.Count == 0) { return 0; }

        u32 sample_count = static_cast<u32>(static_cast<f32>(sorted.Count) * percentile);
        if (sample_count == 0) { sample_count = 1; }

        f32 total_ms = 0;
        for (u32 i = 0; i < sample_count; i++) {
            total_ms += sorted.Values[sorted.Count - 1 - i];
        }

        f32 mean_ms = total_ms / static_cast<f32>(sample_count);
        return mean_ms > 0 ? 1000.0f / mean_ms : 0;
    }

    void Frame(f32 delta_time) {
        f32 frame_time_ms = delta_time * 1000.0f;

        FrameTimesMs[HistoryCursor] = frame_time_ms;
        HistoryCursor = (HistoryCursor + 1) % HistorySize;
        if (HistoryCount < HistorySize) { HistoryCount++; }

        TimeSinceMetricsPoll += delta_time;
        if (TimeSinceMetricsPoll >= MetricsPollInterval) {
            TimeSinceMetricsPoll = 0;
            ProcessMetrics::Poll();
        }

        if (!Visible) { return; }

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

        // Position: Bottom-Right
        const f32 pad = 10.0f;
        const ImVec2 viewport_pos = ImGui::GetMainViewport()->WorkPos;
        const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
        ImVec2 window_pos = { (viewport_pos.x + viewport_size.x - pad), (viewport_pos.y + viewport_size.y - pad) };
        ImVec2 window_pos_pivot = { 1.0f, 1.0f }; // Pivot on bottom-right corner

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

        if (ImGui::Begin("Perf Overlay", nullptr, window_flags)) {
            SortedHistory sorted = GetSortedHistory();

            ImGui::Text("FPS: %.1f (%.3f ms)", frame_time_ms > 0 ? 1000.0f / frame_time_ms : 0, frame_time_ms);
            ImGui::Separator();

            ImGui::Text("1%%  low: %.1f fps", PercentileLowFps(sorted, 0.01f));
            ImGui::Text("0.1%% low: %.1f fps", PercentileLowFps(sorted, 0.001f));

            if (sorted.Count > 0) {
                ImGui::Text("Median:   %.3f ms", sorted.Values[sorted.Count / 2]);
                ImGui::Text("Worst:    %.3f ms", sorted.Values[sorted.Count - 1]);
            }

            ImGui::Separator();
            ImGui::Text("CPU: %.1f%%", static_cast<f64>(ProcessMetrics::CpuPercent));
            ImGui::Text("Mem: %.1f MB", static_cast<f64>(ProcessMetrics::WorkingSetMb));

            ImGui::Separator();

            // The ring buffer is plotted through PlotLines' offset parameter so the graph scrolls
            // instead of jumping when the cursor wraps.
            f32 scale_max = sorted.Count > 0 ? sorted.Values[sorted.Count - 1] * 1.2f : 33.3f;
            ImGui::PlotLines(
                "##FrameTimes",
                FrameTimesMs.data(),
                static_cast<int>(HistoryCount),
                static_cast<int>(HistoryCursor),
                "frame time (ms)",
                0.0f,
                scale_max,
                ImVec2(220, 60)
            );
        }
        ImGui::End();
    }
}
