#include "PerfOverlay.hpp"

#include <algorithm>
#include <array>

#include <imgui.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

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

    f32 MemoryUsageMb = 0;
    f32 CpuUsagePercent = 0;

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

#if defined(_WIN32)
    u64 ToU64(const FILETIME& file_time) {
        return (static_cast<u64>(file_time.dwHighDateTime) << 32) | file_time.dwLowDateTime;
    }

    void PollProcessMetrics() {
        PROCESS_MEMORY_COUNTERS memory_counters;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory_counters, sizeof(memory_counters))) {
            MemoryUsageMb = static_cast<f32>(memory_counters.WorkingSetSize) / (1024.0f * 1024.0f);
        }

        // CPU usage is a delta between polls, so the first poll only establishes a baseline.
        static u64 last_process_time = 0;
        static u64 last_wall_time = 0;

        FILETIME creation_time, exit_time, kernel_time, user_time;
        if (!GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time)) {
            return;
        }

        FILETIME now_file_time;
        GetSystemTimeAsFileTime(&now_file_time);

        u64 process_time = ToU64(kernel_time) + ToU64(user_time);
        u64 wall_time = ToU64(now_file_time);

        if (last_wall_time != 0 && wall_time > last_wall_time) {
            u64 process_delta = process_time - last_process_time;
            u64 wall_delta = wall_time - last_wall_time;

            SYSTEM_INFO system_info;
            GetSystemInfo(&system_info);

            f32 usage = (static_cast<f32>(process_delta) / static_cast<f32>(wall_delta)) * 100.0f;
            CpuUsagePercent = usage / static_cast<f32>(system_info.dwNumberOfProcessors);
        }

        last_process_time = process_time;
        last_wall_time = wall_time;
    }
#else
    void PollProcessMetrics() {
        FILE* status = std::fopen("/proc/self/statm", "r");
        if (status) {
            u64 total_pages = 0;
            u64 resident_pages = 0;
            if (std::fscanf(status, "%llu %llu", (unsigned long long*)&total_pages, (unsigned long long*)&resident_pages) == 2) {
                f32 page_size = static_cast<f32>(sysconf(_SC_PAGESIZE));
                MemoryUsageMb = (static_cast<f32>(resident_pages) * page_size) / (1024.0f * 1024.0f);
            }
            std::fclose(status);
        }

        // CPU usage is a delta between polls, so the first poll only establishes a baseline.
        static u64 last_process_ticks = 0;
        static u64 last_wall_ticks = 0;

        FILE* stat = std::fopen("/proc/self/stat", "r");
        if (!stat) { return; }

        // Fields 14 (utime) and 15 (stime) of /proc/self/stat, skipping the leading pid and the
        // comm field (which can itself contain spaces, hence the %*[^)] skip).
        u64 utime = 0;
        u64 stime = 0;
        int scanned = std::fscanf(
            stat,
            "%*d (%*[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
            (unsigned long long*)&utime,
            (unsigned long long*)&stime
        );
        std::fclose(stat);

        if (scanned != 2) { return; }

        u64 process_ticks = utime + stime;
        u64 wall_ticks = static_cast<u64>(clock());

        if (last_wall_ticks != 0 && wall_ticks > last_wall_ticks) {
            f32 ticks_per_second = static_cast<f32>(sysconf(_SC_CLK_TCK));
            f32 process_seconds = static_cast<f32>(process_ticks - last_process_ticks) / ticks_per_second;
            f32 wall_seconds = static_cast<f32>(wall_ticks - last_wall_ticks) / static_cast<f32>(CLOCKS_PER_SEC);

            if (wall_seconds > 0) {
                f32 core_count = static_cast<f32>(sysconf(_SC_NPROCESSORS_ONLN));
                CpuUsagePercent = (process_seconds / wall_seconds) * 100.0f / core_count;
            }
        }

        last_process_ticks = process_ticks;
        last_wall_ticks = wall_ticks;
    }
#endif

    void Frame(f32 delta_time) {
        f32 frame_time_ms = delta_time * 1000.0f;

        FrameTimesMs[HistoryCursor] = frame_time_ms;
        HistoryCursor = (HistoryCursor + 1) % HistorySize;
        if (HistoryCount < HistorySize) { HistoryCount++; }

        TimeSinceMetricsPoll += delta_time;
        if (TimeSinceMetricsPoll >= MetricsPollInterval) {
            TimeSinceMetricsPoll = 0;
            PollProcessMetrics();
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
            ImGui::Text("CPU: %.1f%%", static_cast<f64>(CpuUsagePercent));
            ImGui::Text("Mem: %.1f MB", static_cast<f64>(MemoryUsageMb));

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
