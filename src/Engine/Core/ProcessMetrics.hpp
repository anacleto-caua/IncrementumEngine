#pragma once

// Process-wide CPU/memory numbers straight from OS APIs (no dependencies). Shared by PerfOverlay
// (which owns the polling cadence) and the benchmark recorder (which only reads the fields).
namespace ProcessMetrics {
    inline f32 WorkingSetMb = 0;
    // Process CPU time over wall time since the previous Poll(), normalized by logical processor
    // count (100% = every core busy). The first Poll() only establishes a baseline.
    inline f32 CpuPercent = 0;

    void Poll();
}
