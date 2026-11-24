#pragma once

#include <chrono>

// CPU-side wall time of the frame loop's blocking or heavy calls, overwritten every loop
// iteration. Only read by the benchmark's per-frame record, to tell where a long frame went
// (waiting on the GPU, inside the driver, or in the terrain update) - GPU timestamps alone can't.
namespace FrameProfile {
    using Clock = std::chrono::steady_clock;

    struct Sample {
        f32 FenceWaitMs = 0.0f;         // Renderer::Frame(): wait for the frame-in-flight slot
        f32 AcquireMs = 0.0f;           // vkAcquireNextImageKHR
        f32 RecordMs = 0.0f;            // command recording, between acquire and submit
        f32 SubmitMs = 0.0f;            // reclaim + AcquirePending + vkQueueSubmit2
        f32 PresentMs = 0.0f;           // vkQueuePresentKHR
        f32 TerrainMs = 0.0f;           // TerrainManager::RefreshChunks(), including the line below
        f32 TransferSubmitMs = 0.0f;    // its SubmitReleaseAndWrite(), 0 when nothing finalized
        f32 TransferReleaseMs = 0.0f;   //   of which: ownership-release submissions
        f32 TransferRecordMs = 0.0f;    //   of which: LazyWrite() recording
        f32 TransferQueueSubmitMs = 0.0f; // of which: the pile's vkQueueSubmit2
    };

    inline Sample Current;

    inline f32 MsSince(Clock::time_point start) {
        return std::chrono::duration<f32, std::milli>(Clock::now() - start).count();
    }
}
