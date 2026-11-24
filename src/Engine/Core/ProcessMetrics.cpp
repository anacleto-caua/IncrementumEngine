#include "ProcessMetrics.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <cstdio>
#include <time.h>
#include <unistd.h>
#endif

namespace ProcessMetrics {
#if defined(_WIN32)
    u64 ToU64(const FILETIME& file_time) {
        return (static_cast<u64>(file_time.dwHighDateTime) << 32) | file_time.dwLowDateTime;
    }

    void Poll() {
        PROCESS_MEMORY_COUNTERS memory_counters;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory_counters, sizeof(memory_counters))) {
            WorkingSetMb = static_cast<f32>(memory_counters.WorkingSetSize) / (1024.0f * 1024.0f);
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
            CpuPercent = usage / static_cast<f32>(system_info.dwNumberOfProcessors);
        }

        last_process_time = process_time;
        last_wall_time = wall_time;
    }
#else
    void Poll() {
        FILE* status = std::fopen("/proc/self/statm", "r");
        if (status) {
            u64 total_pages = 0;
            u64 resident_pages = 0;
            if (std::fscanf(status, "%llu %llu", (unsigned long long*)&total_pages, (unsigned long long*)&resident_pages) == 2) {
                f32 page_size = static_cast<f32>(sysconf(_SC_PAGESIZE));
                WorkingSetMb = (static_cast<f32>(resident_pages) * page_size) / (1024.0f * 1024.0f);
            }
            std::fclose(status);
        }

        // CPU usage is a delta between polls, so the first poll only establishes a baseline.
        static u64 last_process_ticks = 0;
        static u64 last_wall_ns = 0;

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

        // Wall time from the monotonic clock. Not clock(): that is this process's own CPU time, which
        // would make the ratio below always 1 (i.e. CpuPercent stuck at 100 / core count).
        timespec now = {};
        clock_gettime(CLOCK_MONOTONIC, &now);

        u64 process_ticks = utime + stime;
        u64 wall_ns = static_cast<u64>(now.tv_sec) * 1000000000ull + static_cast<u64>(now.tv_nsec);

        if (last_wall_ns != 0 && wall_ns > last_wall_ns) {
            f32 ticks_per_second = static_cast<f32>(sysconf(_SC_CLK_TCK));
            f32 process_seconds = static_cast<f32>(process_ticks - last_process_ticks) / ticks_per_second;
            f32 wall_seconds = static_cast<f32>(wall_ns - last_wall_ns) / 1.0e9f;

            if (wall_seconds > 0) {
                f32 core_count = static_cast<f32>(sysconf(_SC_NPROCESSORS_ONLN));
                CpuPercent = (process_seconds / wall_seconds) * 100.0f / core_count;
            }
        }

        last_process_ticks = process_ticks;
        last_wall_ns = wall_ns;
    }
#endif
}
