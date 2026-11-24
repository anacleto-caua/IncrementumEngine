#include "Benchmark.hpp"

#include "Renderer/Camera.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <vma/vk_mem_alloc.h>

#include "BuildInfo.hpp"
#include "Core/JsonWriter.hpp"
#include "Core/Math.hpp"
#include "Game/Game.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"
#include "Engine/Core/FrameProfile.hpp"
#include "Engine/Core/PerfOverlay.hpp"
#include "Engine/Core/Platform.hpp"
#include "Engine/Core/ProcessMetrics.hpp"
#include "Engine/Core/SystemInfo.hpp"
#include "Engine/Core/TaskScheduler/TaskScheduler.hpp"
#include "Renderer/VkVault.hpp"

namespace Benchmark {
    // ---- Scenario definitions ----

    enum class CameraPath : u8 { Static, Line, Spin, SpinMove, Circle };

    const char* CameraPathName(CameraPath path) {
        switch (path) {
            case CameraPath::Static: return "static";
            case CameraPath::Line: return "line";
            case CameraPath::Spin: return "spin";
            case CameraPath::SpinMove: return "spin_move";
            case CameraPath::Circle: return "circle";
        }
        return "?";
    }

    const char* PropModeName(PropPass::DrawMode mode) {
        switch (mode) {
            case PropPass::DrawMode::Instanced: return "instanced";
            case PropPass::DrawMode::PerInstance: return "per_instance";
            case PropPass::DrawMode::Off: return "off";
        }
        return "?";
    }

    struct SegmentDesc {
        std::string Name;
        CameraPath Path = CameraPath::Static;
        f32 Speed = 0.0f;       // world units/s for Line/Circle/SpinMove, degrees/s for Spin
        f32 SpinRate = 0.0f;    // degrees/s, SpinMove only
        f32 Yaw = 0.0f;         // starting yaw, degrees
        bool DrawCulling = true;
        bool GenerationCulling = true;
        PropPass::DrawMode Props = PropPass::DrawMode::Instanced;
        f32 Duration = 60.0f;   // measured seconds (ColdStart: upper bound)
    };

    struct ScenarioDesc {
        std::string Name;
        bool ColdStart = false; // record from the first frame, end shortly after the world completes
        bool PerFrame = true;   // full per-frame columns (off for the soak - ticks only)
        f32 Warmup = 10.0f;     // settle before the first segment, once the world is complete
        f32 Settle = 5.0f;      // settle before every later segment
        std::vector<SegmentDesc> Segments;
    };

    // Every path starts at the world origin (where TerrainManager::Init() seeds ring 0) at a fixed
    // altitude above the generator's maximum height, so no path can clip into a mountain and the
    // view is identical on every machine. Pitch is fixed too.
    constexpr f32 AltitudeAboveMaxHeight = 100.0f;
    constexpr f32 PitchDegrees = -15.0f;
    constexpr f64 CircleRadius = 4000.0;
    constexpr f64 Pi = 3.14159265358979323846;

    constexpr f32 TickInterval = 0.25f;           // per-tick sample cadence (seconds)
    constexpr f32 WorldWaitTimeout = 180.0f;      // give up waiting for a complete world after this
    constexpr f32 ColdStartHold = 2.0f;           // keep recording this long after the world completes
    constexpr f32 ColdStartMaxDuration = 180.0f;
    constexpr f32 StutterFactor = 2.0f;           // stutter = frame time > StutterFactor x segment median
    constexpr u32 RingCount = 1 + TerrainManager::OuterRingCount;

    ScenarioDesc MakeScenario(const std::string& name) {
        ScenarioDesc scenario;
        scenario.Name = name;

        if (name == "c1_coldstart") {
            scenario.ColdStart = true;
            scenario.Segments.push_back({ .Name = "coldstart", .Duration = ColdStartMaxDuration });
        } else if (name == "c2_static") {
            scenario.Segments.push_back({ .Name = "static" });
        } else if (name == "c2_slow") {
            scenario.Segments.push_back({ .Name = "line_slow", .Path = CameraPath::Line, .Speed = 100.0f });
        } else if (name == "c2_fast") {
            scenario.Segments.push_back({ .Name = "line_fast", .Path = CameraPath::Line, .Speed = 500.0f });
        } else if (name == "c2_spin") {
            scenario.Segments.push_back({ .Name = "spin", .Path = CameraPath::Spin, .Speed = 45.0f });
        } else if (name == "c2_spin_move") {
            // Residency is distance-driven, so spinning in place over a complete world never
            // uncovers a missing chunk - generation has to be running for visibility priority to
            // matter. Moving fast along +X while spinning keeps new chunks entering range in
            // every direction the camera sweeps through.
            scenario.Segments.push_back({ .Name = "spin_move", .Path = CameraPath::SpinMove, .Speed = 500.0f, .SpinRate = 45.0f });
        } else if (name == "c3") {
            // C3a: culling on/off at three orientations from the same (fully generated) position.
            // Only draw culling changes - generation priority stays on, and the world is complete
            // before each segment, so the two can't confound each other.
            for (bool culling : { true, false }) {
                for (f32 yaw : { 0.0f, 120.0f, 240.0f }) {
                    scenario.Segments.push_back({
                        .Name = std::string(culling ? "cull_on" : "cull_off") + "_yaw" + std::to_string(static_cast<i32>(yaw)),
                        .Yaw = yaw,
                        .DrawCulling = culling
                    });
                }
            }
            // C3b: prop draw modes (instanced is cull_on_yaw0 above).
            scenario.Segments.push_back({ .Name = "props_per_instance", .Props = PropPass::DrawMode::PerInstance });
            scenario.Segments.push_back({ .Name = "props_off", .Props = PropPass::DrawMode::Off });
        } else if (name == "c4_soak") {
            scenario.PerFrame = false;
            scenario.Segments.push_back({ .Name = "circle", .Path = CameraPath::Circle, .Speed = 100.0f, .Duration = 900.0f });
        } else if (name == "sysinfo") {
            // Machine description only - no frames.
        } else {
            scenario.Name.clear();
        }
        return scenario;
    }

    // ---- Recording ----

    // A named set of equally long f32 columns, written to JSON column by column.
    struct ColumnSet {
        std::vector<std::string> Names;
        std::vector<std::vector<f32>> Values;
        std::vector<i32> Precision;

        u32 Add(std::string name, i32 precision = 6) {
            Names.push_back(std::move(name));
            Values.emplace_back();
            Precision.push_back(precision);
            return static_cast<u32>(Names.size() - 1);
        }

        void Reserve(size_t count) {
            for (auto& column : Values) { column.reserve(count); }
        }

        void Push(u32 column, f32 value) { Values[column].push_back(value); }

        void Write(JsonWriter& json, std::string_view key) const {
            json.Key(key);
            json.BeginObject();
            for (size_t i = 0; i < Names.size(); i++) {
                json.Column(Names[i], Values[i], Precision[i]);
            }
            json.EndObject();
        }
    };

    // GPU timing slots: setup, scene passes, resolve, overlay passes, whole frame.
    constexpr u32 GpuSlotCount = 1 + Renderer::ScenePassNames.size() + 1 + Renderer::OverlayPassNames.size() + 1;

    struct SegmentRecord {
        SegmentDesc Desc;
        f32 WorldWaitSeconds = 0.0f;
        bool WorldWaitTimedOut = false;
        f32 SettleSeconds = 0.0f;

        f32 RecordedSeconds = 0.0f;
        std::vector<f32> FrameMs;   // always kept, for the summary
        ColumnSet Frames;           // only with ScenarioDesc::PerFrame
        ColumnSet Ticks;

        // Column indices.
        u32 FrameT = 0, FrameMsCol = 0, FrameGpuMs = 0, FrameDark = 0, FrameDrawn = 0, FrameCulled = 0,
            FrameMissing = 0, FrameInFlight = 0, FrameGenerated = 0, FramePropDraws = 0,
            FrameFenceWait = 0, FrameAcquire = 0, FrameRecord = 0, FrameSubmit = 0, FramePresent = 0,
            FrameTerrain = 0, FrameTransferSubmit = 0, FrameTransferRelease = 0, FrameTransferRecord = 0,
            FrameTransferQueueSubmit = 0;
        u32 TickT = 0, TickFrames = 0, TickFrameMean = 0, TickFrameMax = 0, TickRam = 0, TickCpu = 0,
            TickVramUsed = 0, TickVramBudget = 0, TickCamX = 0, TickCamZ = 0, TickYaw = 0,
            TickPropDraws = 0, TickPropInstances = 0;
        std::array<u32, GpuSlotCount> TickGpu = {};
        std::array<u32, RingCount> TickRingDrawn = {}, TickRingCulled = {}, TickRingDark = {},
            TickRingMissing = {}, TickRingInFlight = {}, TickRingGenerated = {}, TickRingEvictions = {};

        // Summary accumulators.
        u64 DarkFrames = 0;
        u32 MaxDark = 0;
        f64 DarkSum = 0, DrawnSum = 0, CulledSum = 0, PropDrawSum = 0;
        u64 GpuSamples = 0;
        std::array<f64, GpuSlotCount> GpuSums = {};
        std::array<u64, RingCount> GeneratedAtStart = {}, EvictionsAtStart = {};
        std::array<u64, RingCount> GeneratedAtEnd = {}, EvictionsAtEnd = {};
        size_t GenerationLogStart = 0, GenerationLogEnd = 0;
        f32 WorldCompleteAt = -1.0f;    // segment time the world was first complete (cold start)
    };

    // Counts warnings/errors from anywhere in the engine and keeps the first few, so a run that
    // logged validation errors or pool overflows is visible in its record instead of only on a
    // console nobody watched.
    class CaptureSink : public spdlog::sinks::base_sink<std::mutex> {
    public:
        u32 Warnings = 0;
        u32 Errors = 0;
        std::vector<std::string> Messages;

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override {
            if (msg.level < spdlog::level::warn) { return; }
            if (msg.level == spdlog::level::warn) { Warnings++; } else { Errors++; }
            if (Messages.size() < 32) { Messages.emplace_back(msg.payload.data(), msg.payload.size()); }
        }
        void flush_() override {}
    };

    enum class Phase : u8 { WaitWorld, Settle, Record, Done };

    BenchConfig Config;
    ScenarioDesc Scenario;
    std::vector<SegmentRecord> Segments;
    std::shared_ptr<CaptureSink> LogCapture;

    Camera* TrackedCamera = nullptr;
    Phase CurrentPhase = Phase::Done;
    u32 SegmentIndex = 0;
    f32 PhaseTime = 0.0f;
    f32 PathTime = 0.0f;
    f32 LastDelta = 0.0f;
    u64 FrameCount = 0;
    bool WorldComplete = false;

    // Tick accumulators.
    f32 TickTime = 0.0f;
    u32 TickFrameCount = 0;
    f64 TickFrameSum = 0.0;
    f32 TickFrameMaxMs = 0.0f;
    u32 TickGpuCount = 0;
    std::array<f64, GpuSlotCount> TickGpuSums = {};
    u64 LastGpuSerial = 0;
    FrameProfile::Sample PrevCpuSample;

    // Timeline (seconds since process start, < 0 = not reached).
    f32 EngineInitMs = 0.0f;
    f32 FirstFrameSeconds = -1.0f;
    f32 WorldCompleteSeconds = -1.0f;

    std::atomic<bool> WatchdogDisarmed = false;

    f32 SecondsSinceStart() {
        return std::chrono::duration<f32>(std::chrono::steady_clock::now() - Config.ProcessStart).count();
    }

    template<typename Fn>
    void ForEachRing(Fn&& fn) {
        fn(0u, TerrainManager::Ring0.DebugStats);
        for (u32 i = 0; i < TerrainManager::OuterRingCount; i++) {
            fn(i + 1, TerrainManager::OuterRings[i].DebugStats);
        }
    }

    bool ComputeWorldComplete() {
        return TerrainManager::IsWorldComplete();
    }

    void ApplyPose(const SegmentDesc& segment, f32 time) {
        f64 yaw = static_cast<f64>(segment.Yaw);
        f64 altitude = static_cast<f64>(GTerrainPass.Config.HeightScale + AltitudeAboveMaxHeight);
        f64 x = 0.0;
        f64 z = 0.0;
        f64 t = static_cast<f64>(time);
        f64 speed = static_cast<f64>(segment.Speed);

        switch (segment.Path) {
            case CameraPath::Static:
                break;
            case CameraPath::Line: {
                f64 yaw_radians = yaw * (Pi / 180.0);
                x = std::cos(yaw_radians) * speed * t;
                z = std::sin(yaw_radians) * speed * t;
                break;
            }
            case CameraPath::Spin:
                yaw += speed * t;
                break;
            case CameraPath::SpinMove:
                x = speed * t;
                yaw += static_cast<f64>(segment.SpinRate) * t;
                break;
            case CameraPath::Circle: {
                // Starts at the origin heading along the segment's yaw, turning steadily so the
                // heading always follows the circle's tangent: closed loop, revisits cached terrain.
                f64 start_heading = yaw * (Pi / 180.0);
                f64 heading = start_heading + speed * t / CircleRadius;
                f64 center_x = -CircleRadius * std::sin(start_heading);
                f64 center_z = CircleRadius * std::cos(start_heading);
                x = center_x + CircleRadius * std::sin(heading);
                z = center_z - CircleRadius * std::cos(heading);
                yaw = heading * (180.0 / Pi);
                break;
            }
        }

        f32 yaw_radians = static_cast<f32>(yaw * (Pi / 180.0));
        f32 pitch_radians = math::radians(PitchDegrees);

        TrackedCamera->Position = vec3(static_cast<f32>(x), static_cast<f32>(altitude), static_cast<f32>(z));
        TrackedCamera->Front = normalize(vec3(
            std::cos(yaw_radians) * std::cos(pitch_radians),
            std::sin(pitch_radians),
            std::sin(yaw_radians) * std::cos(pitch_radians)
        ));
        UpdateMatrices(*TrackedCamera);
    }

    void ApplySegmentToggles(const SegmentDesc& segment) {
        TerrainManager::DrawCullingEnabled = segment.DrawCulling;
        TerrainManager::CullingPrioritizesGeneration = segment.GenerationCulling;
        GPropPass.Mode = segment.Props;
    }

    void SetupColumns(SegmentRecord& record) {
        if (Scenario.PerFrame) {
            ColumnSet& f = record.Frames;
            record.FrameT = f.Add("t", 7);
            record.FrameMsCol = f.Add("frame_ms", 5);
            // GPU time lags the CPU frame by MAX_FRAMES_IN_FLIGHT frames; null on frames where no
            // new result landed.
            record.FrameGpuMs = f.Add("gpu_ms", 5);
            record.FrameDark = f.Add("dark");
            record.FrameDrawn = f.Add("drawn");
            record.FrameCulled = f.Add("culled");
            record.FrameMissing = f.Add("missing");
            record.FrameInFlight = f.Add("in_flight");
            record.FrameGenerated = f.Add("generated");     // cumulative within the segment
            record.FramePropDraws = f.Add("prop_draw_calls");
            // CPU wall time of the same loop iteration frame_ms measured (FrameProfile.hpp).
            record.FrameFenceWait = f.Add("cpu_fence_wait_ms", 4);
            record.FrameAcquire = f.Add("cpu_acquire_ms", 4);
            record.FrameRecord = f.Add("cpu_record_ms", 4);
            record.FrameSubmit = f.Add("cpu_submit_ms", 4);
            record.FramePresent = f.Add("cpu_present_ms", 4);
            record.FrameTerrain = f.Add("cpu_terrain_ms", 4);
            record.FrameTransferSubmit = f.Add("cpu_transfer_submit_ms", 4);
            record.FrameTransferRelease = f.Add("cpu_transfer_release_ms", 4);
            record.FrameTransferRecord = f.Add("cpu_transfer_record_ms", 4);
            record.FrameTransferQueueSubmit = f.Add("cpu_transfer_queue_submit_ms", 4);
        }

        ColumnSet& k = record.Ticks;
        record.TickT = k.Add("t", 7);
        record.TickFrames = k.Add("frames");
        record.TickFrameMean = k.Add("frame_ms_mean", 5);
        record.TickFrameMax = k.Add("frame_ms_max", 5);

        u32 slot = 0;
        record.TickGpu[slot++] = k.Add("gpu_setup_ms", 4);
        for (const char* name : Renderer::ScenePassNames) { record.TickGpu[slot++] = k.Add(std::string("gpu_") + name + "_ms", 4); }
        record.TickGpu[slot++] = k.Add("gpu_resolve_ms", 4);
        for (const char* name : Renderer::OverlayPassNames) { record.TickGpu[slot++] = k.Add(std::string("gpu_") + name + "_ms", 4); }
        record.TickGpu[slot++] = k.Add("gpu_frame_ms", 4);

        record.TickRam = k.Add("ram_mb", 6);
        record.TickCpu = k.Add("cpu_pct", 4);
        record.TickVramUsed = k.Add("vram_used_mb", 6);
        record.TickVramBudget = k.Add("vram_budget_mb", 6);
        record.TickCamX = k.Add("cam_x", 7);
        record.TickCamZ = k.Add("cam_z", 7);
        record.TickYaw = k.Add("cam_yaw", 5);
        record.TickPropDraws = k.Add("prop_draw_calls");
        record.TickPropInstances = k.Add("prop_instances");

        for (u32 r = 0; r < RingCount; r++) {
            std::string prefix = "ring" + std::to_string(r) + "_";
            record.TickRingDrawn[r] = k.Add(prefix + "drawn");
            record.TickRingCulled[r] = k.Add(prefix + "culled");
            record.TickRingDark[r] = k.Add(prefix + "dark");
            record.TickRingMissing[r] = k.Add(prefix + "missing");
            record.TickRingInFlight[r] = k.Add(prefix + "in_flight");
            record.TickRingGenerated[r] = k.Add(prefix + "generated");  // cumulative within the segment
            record.TickRingEvictions[r] = k.Add(prefix + "evictions");  // cumulative within the segment
        }
    }

    // Reserves for the whole segment up front (outside the measured window), so recording never
    // reallocates mid-measurement. 3000 fps is a deliberately generous ceiling - if a machine
    // exceeds it, the vectors just grow.
    void ReserveRecord(SegmentRecord& record) {
        size_t expected_frames = static_cast<size_t>(record.Desc.Duration * 3000.0f) + 1024;
        record.FrameMs.reserve(expected_frames);
        if (Scenario.PerFrame) { record.Frames.Reserve(expected_frames); }
        record.Ticks.Reserve(static_cast<size_t>(record.Desc.Duration / TickInterval) + 64);
    }

    void ResetTick() {
        TickTime = 0.0f;
        TickFrameCount = 0;
        TickFrameSum = 0.0;
        TickFrameMaxMs = 0.0f;
        TickGpuCount = 0;
        TickGpuSums = {};
    }

    void StartRecording(SegmentRecord& record) {
        CurrentPhase = Phase::Record;
        PathTime = 0.0f;
        ResetTick();
        ForEachRing([&](u32 r, const TerrainManager::Stats& stats) {
            record.GeneratedAtStart[r] = stats.ChunksGenerated;
            record.EvictionsAtStart[r] = stats.Evictions;
        });
        record.GenerationLogStart = TerrainManager::GenerationLog.size();
    }

    void FlushTick(SegmentRecord& record) {
        if (TickFrameCount == 0) { return; }
        ColumnSet& k = record.Ticks;

        k.Push(record.TickT, PathTime);
        k.Push(record.TickFrames, static_cast<f32>(TickFrameCount));
        k.Push(record.TickFrameMean, static_cast<f32>(TickFrameSum / TickFrameCount));
        k.Push(record.TickFrameMax, TickFrameMaxMs);
        for (u32 slot = 0; slot < GpuSlotCount; slot++) {
            k.Push(record.TickGpu[slot], TickGpuCount > 0 ? static_cast<f32>(TickGpuSums[slot] / TickGpuCount) : NAN);
        }

        k.Push(record.TickRam, ProcessMetrics::WorkingSetMb);
        k.Push(record.TickCpu, ProcessMetrics::CpuPercent);

        VkPhysicalDeviceMemoryProperties const* memory_properties = nullptr;
        vmaGetMemoryProperties(VkVault::VmaAllocator, &memory_properties);
        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets = {};
        vmaGetHeapBudgets(VkVault::VmaAllocator, budgets.data());
        f64 vram_used = 0.0;
        f64 vram_budget = 0.0;
        for (u32 heap = 0; heap < memory_properties->memoryHeapCount; heap++) {
            if (memory_properties->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                vram_used += static_cast<f64>(budgets[heap].usage);
                vram_budget += static_cast<f64>(budgets[heap].budget);
            }
        }
        k.Push(record.TickVramUsed, static_cast<f32>(vram_used / (1024.0 * 1024.0)));
        k.Push(record.TickVramBudget, static_cast<f32>(vram_budget / (1024.0 * 1024.0)));

        f32 yaw_degrees = std::atan2(TrackedCamera->Front.z, TrackedCamera->Front.x) * (180.0f / static_cast<f32>(Pi));
        k.Push(record.TickCamX, TrackedCamera->Position.x);
        k.Push(record.TickCamZ, TrackedCamera->Position.z);
        k.Push(record.TickYaw, yaw_degrees);
        k.Push(record.TickPropDraws, static_cast<f32>(GPropPass.DrawCallsLastFrame));
        k.Push(record.TickPropInstances, static_cast<f32>(GPropPass.InstancesLastFrame));

        ForEachRing([&](u32 r, const TerrainManager::Stats& stats) {
            k.Push(record.TickRingDrawn[r], static_cast<f32>(stats.DrawnLastFrame));
            k.Push(record.TickRingCulled[r], static_cast<f32>(stats.CulledLastFrame));
            k.Push(record.TickRingDark[r], static_cast<f32>(stats.VisibleMissingLastFrame));
            k.Push(record.TickRingMissing[r], static_cast<f32>(stats.MissingLastFrame));
            k.Push(record.TickRingInFlight[r], static_cast<f32>(stats.GenerationsInFlight));
            k.Push(record.TickRingGenerated[r], static_cast<f32>(stats.ChunksGenerated - record.GeneratedAtStart[r]));
            k.Push(record.TickRingEvictions[r], static_cast<f32>(stats.Evictions - record.EvictionsAtStart[r]));
        });

        ResetTick();
    }

    void EnterSegment(u32 index) {
        SegmentIndex = index;
        PhaseTime = 0.0f;
        ApplySegmentToggles(Segments[index].Desc);
        ApplyPose(Segments[index].Desc, 0.0f);
        CurrentPhase = Phase::WaitWorld;
    }

    void EndSegment(SegmentRecord& record) {
        FlushTick(record);
        ForEachRing([&](u32 r, const TerrainManager::Stats& stats) {
            record.GeneratedAtEnd[r] = stats.ChunksGenerated;
            record.EvictionsAtEnd[r] = stats.Evictions;
        });
        record.GenerationLogEnd = TerrainManager::GenerationLog.size();

        if (SegmentIndex + 1 < Segments.size()) {
            EnterSegment(SegmentIndex + 1);
        } else {
            CurrentPhase = Phase::Done;
            Platform::RequestClose();
        }
    }

    // ---- Lifecycle ----

    bool ParseArgs(i32 argc, char** argv, BenchConfig& out_config) {
        for (i32 i = 1; i < argc; i++) {
            std::string_view arg = argv[i];
            auto equals = arg.find('=');
            if (!arg.starts_with("--") || equals == std::string_view::npos) {
                analog::error("unrecognized argument '{}' (expected --key=value)", arg);
                return false;
            }
            std::string_view key = arg.substr(2, equals - 2);
            std::string value(arg.substr(equals + 1));

            if (key == "bench") { out_config.Scenario = value; }
            else if (key == "out") { out_config.OutPath = value; }
            else if (key == "run") { out_config.Run = static_cast<u32>(std::stoul(value)); }
            else if (key == "workers") { out_config.WorkersArg = value; }
            else if (key == "width") { out_config.Width = static_cast<u32>(std::stoul(value)); }
            else if (key == "height") { out_config.Height = static_cast<u32>(std::stoul(value)); }
            else if (key == "duration") { out_config.DurationOverride = std::stof(value); }
            else if (key == "warmup") { out_config.WarmupOverride = std::stof(value); }
            else if (key == "handoff") {
                if (value != "full" && value != "fallback" && value != "prefetch" && value != "off") {
                    analog::error("--handoff must be full, fallback, prefetch or off");
                    return false;
                }
                out_config.Handoff = value;
                TerrainManager::HandoffFallbackEnabled = (value == "full" || value == "fallback");
                TerrainManager::HandoffPrefetchEnabled = (value == "full" || value == "prefetch");
            }
            else {
                analog::error("unrecognized argument '--{}'", key);
                return false;
            }
        }

        if (out_config.Scenario.empty()) { return true; }

        if (MakeScenario(out_config.Scenario).Name.empty()) {
            analog::error("unknown benchmark scenario '{}'", out_config.Scenario);
            return false;
        }

        Active = true;

        // Installed now, before any engine init, so init-time warnings are counted too.
        LogCapture = std::make_shared<CaptureSink>();
        spdlog::default_logger()->sinks().push_back(LogCapture);
        return true;
    }

    bool ResolveWorkers(BenchConfig& config) {
        u32 logical = std::max<u32>(std::thread::hardware_concurrency(), 1);
        u32 max_meaningful = std::max<u32>(logical - 1, 1);

        if (config.WorkersArg.empty() || config.WorkersArg == "half") {
            config.Workers = std::max<u32>(logical / 2, 1);
        } else if (config.WorkersArg == "max-1") {
            config.Workers = max_meaningful;
        } else {
            config.Workers = static_cast<u32>(std::stoul(config.WorkersArg));
            if (config.Workers == 0 || config.Workers > max_meaningful) { return false; }
        }
        return true;
    }

    bool AppendLine(const std::string& path, const std::string& line) {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file) {
            analog::error("couldn't open benchmark output '{}'", path);
            return false;
        }
        file.write(line.data(), static_cast<std::streamsize>(line.size()));
        file.put('\n');
        file.close();
        if (!file) {
            analog::error("couldn't write benchmark output '{}'", path);
            return false;
        }
        return true;
    }

    void WriteCommonFields(JsonWriter& json, const BenchConfig& config) {
        json.Field("scenario", config.Scenario);
        json.Field("run", config.Run);
        json.Field("commit", INC_GIT_COMMIT);
        json.Field("commit_dirty", INC_GIT_DIRTY != 0);
        json.Field("workers_arg", config.WorkersArg.empty() ? std::string("half") : config.WorkersArg);
        json.Field("handoff", config.Handoff);
    }

    void WriteSkipped(const BenchConfig& config, const char* reason) {
        JsonWriter json;
        json.BeginObject();
        json.Field("type", "skipped");
        WriteCommonFields(json, config);
        json.Field("logical_threads", std::thread::hardware_concurrency());
        json.Field("reason", reason);
        json.EndObject();
        AppendLine(config.OutPath, json.Out);
    }

    void Init(const BenchConfig& config, Camera& camera, f32 engine_init_ms) {
        Config = config;
        Scenario = MakeScenario(config.Scenario);
        TrackedCamera = &camera;
        EngineInitMs = engine_init_ms;

        // Observer cost held identical across runs: no perf overlay (debug windows already start
        // closed), sun fixed instead of animated so lighting doesn't drift between segments.
        PerfOverlay::Visible = false;
        GSkyPass.AnimateSun = false;
        GSkyPass.SunAzimuthDegrees = 30.0f;
        GSkyPass.SunElevationDegrees = 45.0f;

        TerrainManager::RecordGenerations = true;
        TerrainManager::GenerationLog.reserve(1 << 18);

        if (Scenario.Segments.empty()) {
            CurrentPhase = Phase::Done;
            Platform::RequestClose();
            return;
        }

        f32 expected_seconds = 0.0f;
        for (u32 i = 0; i < Scenario.Segments.size(); i++) {
            SegmentRecord record;
            record.Desc = Scenario.Segments[i];
            if (config.DurationOverride > 0.0f) { record.Desc.Duration = config.DurationOverride; }
            if (Scenario.ColdStart) {
                record.SettleSeconds = 0.0f;
            } else {
                record.SettleSeconds = i == 0 ? Scenario.Warmup : Scenario.Settle;
                if (config.WarmupOverride >= 0.0f) { record.SettleSeconds = config.WarmupOverride; }
            }
            SetupColumns(record);
            Segments.push_back(std::move(record));
            expected_seconds += Segments.back().SettleSeconds + Segments.back().Desc.Duration + WorldWaitTimeout;
        }

        // A hung run (device lost, a deadlock) would otherwise stall the whole unattended suite.
        // The driver script records the nonzero exit code as a failed run and moves on.
        f32 deadline_seconds = expected_seconds + 120.0f;
        std::thread([deadline_seconds]() {
            while (!WatchdogDisarmed.load(std::memory_order_acquire)) {
                if (SecondsSinceStart() > deadline_seconds) {
                    std::fprintf(stderr, "benchmark watchdog: run exceeded %.0f s, aborting\n", static_cast<f64>(deadline_seconds));
                    std::fflush(stderr);
                    std::_Exit(3);
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();

        EnterSegment(0);
        if (Scenario.ColdStart) {
            ReserveRecord(Segments[0]);
            StartRecording(Segments[0]);
        }
    }

    void BeginFrame(f32 delta_time) {
        if (!Active || CurrentPhase == Phase::Done) { return; }

        FrameCount++;
        if (FrameCount == 1) {
            FirstFrameSeconds = SecondsSinceStart();
            // The first DeltaTime spans everything since FrameTimer's construction (all of engine
            // init) - not a frame, so it neither advances the path nor gets recorded.
            delta_time = 0.0f;
        }
        LastDelta = delta_time;

        SegmentRecord& record = Segments[SegmentIndex];

        switch (CurrentPhase) {
            case Phase::WaitWorld:
                PhaseTime += delta_time;
                if (WorldComplete || PhaseTime >= WorldWaitTimeout) {
                    record.WorldWaitSeconds = PhaseTime;
                    record.WorldWaitTimedOut = !WorldComplete;
                    PhaseTime = 0.0f;
                    CurrentPhase = Phase::Settle;
                    ReserveRecord(record);
                }
                break;
            case Phase::Settle:
                PhaseTime += delta_time;
                if (PhaseTime >= record.SettleSeconds) {
                    StartRecording(record);
                }
                break;
            case Phase::Record:
                PathTime += delta_time;
                ApplyPose(record.Desc, PathTime);
                break;
            case Phase::Done:
                break;
        }
    }

    void EndFrame() {
        if (!Active || CurrentPhase == Phase::Done) { return; }

        // frame_ms below is LastDelta, the *previous* loop iteration's duration (Timer.Tick() runs
        // at the top of Engine::Frame()), so pair it with the CPU timings of that same iteration.
        const FrameProfile::Sample cpu = PrevCpuSample;
        PrevCpuSample = FrameProfile::Current;

        WorldComplete = ComputeWorldComplete();
        if (WorldComplete && WorldCompleteSeconds < 0.0f) { WorldCompleteSeconds = SecondsSinceStart(); }

        if (CurrentPhase != Phase::Record || FrameCount == 1) { return; }

        SegmentRecord& record = Segments[SegmentIndex];
        f32 frame_ms = LastDelta * 1000.0f;

        u32 dark = 0, drawn = 0, culled = 0, missing = 0, in_flight = 0;
        u64 generated = 0;
        ForEachRing([&](u32 r, const TerrainManager::Stats& stats) {
            dark += stats.VisibleMissingLastFrame;
            drawn += stats.DrawnLastFrame;
            culled += stats.CulledLastFrame;
            missing += stats.MissingLastFrame;
            in_flight += stats.GenerationsInFlight;
            generated += stats.ChunksGenerated - record.GeneratedAtStart[r];
        });

        // A GPU result only lands when a retired frame was read back this frame.
        const Renderer::GpuTimingData& gpu = GRenderer.GpuTimings;
        bool new_gpu_sample = gpu.Available && gpu.FrameSerial != LastGpuSerial;
        LastGpuSerial = gpu.FrameSerial;
        std::array<f32, GpuSlotCount> gpu_slots = {};
        if (new_gpu_sample) {
            u32 slot = 0;
            gpu_slots[slot++] = gpu.SetupMs;
            for (f32 ms : gpu.ScenePassMs) { gpu_slots[slot++] = ms; }
            gpu_slots[slot++] = gpu.ResolveMs;
            for (f32 ms : gpu.OverlayPassMs) { gpu_slots[slot++] = ms; }
            gpu_slots[slot++] = gpu.FrameMs;
            for (u32 i = 0; i < GpuSlotCount; i++) {
                record.GpuSums[i] += static_cast<f64>(gpu_slots[i]);
                TickGpuSums[i] += static_cast<f64>(gpu_slots[i]);
            }
            record.GpuSamples++;
            TickGpuCount++;
        }

        record.FrameMs.push_back(frame_ms);
        record.RecordedSeconds += LastDelta;
        if (dark > 0) { record.DarkFrames++; }
        record.MaxDark = std::max(record.MaxDark, dark);
        record.DarkSum += dark;
        record.DrawnSum += drawn;
        record.CulledSum += culled;
        record.PropDrawSum += GPropPass.DrawCallsLastFrame;

        if (Scenario.PerFrame) {
            ColumnSet& f = record.Frames;
            f.Push(record.FrameT, PathTime);
            f.Push(record.FrameMsCol, frame_ms);
            f.Push(record.FrameGpuMs, new_gpu_sample ? gpu.FrameMs : NAN);
            f.Push(record.FrameDark, static_cast<f32>(dark));
            f.Push(record.FrameDrawn, static_cast<f32>(drawn));
            f.Push(record.FrameCulled, static_cast<f32>(culled));
            f.Push(record.FrameMissing, static_cast<f32>(missing));
            f.Push(record.FrameInFlight, static_cast<f32>(in_flight));
            f.Push(record.FrameGenerated, static_cast<f32>(generated));
            f.Push(record.FramePropDraws, static_cast<f32>(GPropPass.DrawCallsLastFrame));
            f.Push(record.FrameFenceWait, cpu.FenceWaitMs);
            f.Push(record.FrameAcquire, cpu.AcquireMs);
            f.Push(record.FrameRecord, cpu.RecordMs);
            f.Push(record.FrameSubmit, cpu.SubmitMs);
            f.Push(record.FramePresent, cpu.PresentMs);
            f.Push(record.FrameTerrain, cpu.TerrainMs);
            f.Push(record.FrameTransferSubmit, cpu.TransferSubmitMs);
            f.Push(record.FrameTransferRelease, cpu.TransferReleaseMs);
            f.Push(record.FrameTransferRecord, cpu.TransferRecordMs);
            f.Push(record.FrameTransferQueueSubmit, cpu.TransferQueueSubmitMs);
        }

        TickTime += LastDelta;
        TickFrameCount++;
        TickFrameSum += frame_ms;
        TickFrameMaxMs = std::max(TickFrameMaxMs, frame_ms);
        if (TickTime >= TickInterval) { FlushTick(record); }

        if (Scenario.ColdStart) {
            if (WorldComplete && record.WorldCompleteAt < 0.0f) { record.WorldCompleteAt = PathTime; }
            if (record.WorldCompleteAt >= 0.0f && PathTime >= record.WorldCompleteAt + ColdStartHold) {
                EndSegment(record);
                return;
            }
        }
        if (PathTime >= record.Desc.Duration) {
            EndSegment(record);
        }
    }

    // ---- Output ----

    // Value at percentile p (0..1) of an ascending-sorted vector, nearest-rank.
    template<typename T>
    T Percentile(const std::vector<T>& sorted, f64 p) {
        if (sorted.empty()) { return T{}; }
        size_t rank = static_cast<size_t>(std::ceil(p * static_cast<f64>(sorted.size())));
        return sorted[std::clamp<size_t>(rank, 1, sorted.size()) - 1];
    }

    void WriteFrameSummary(JsonWriter& json, const SegmentRecord& record) {
        std::vector<f32> sorted = record.FrameMs;
        std::sort(sorted.begin(), sorted.end());
        size_t count = sorted.size();

        f64 sum = 0.0;
        for (f32 ms : sorted) { sum += static_cast<f64>(ms); }

        // "x% low" = mean of the slowest x% of frames, as frame time (not FPS).
        auto slowest_mean = [&](f64 fraction) {
            if (count == 0) { return 0.0; }
            size_t n = std::max<size_t>(static_cast<size_t>(static_cast<f64>(count) * fraction), 1);
            f64 total = 0.0;
            for (size_t i = 0; i < n; i++) { total += static_cast<f64>(sorted[count - 1 - i]); }
            return total / static_cast<f64>(n);
        };

        f32 median = Percentile(sorted, 0.5);
        u64 stutters = 0;
        for (f32 ms : record.FrameMs) { if (ms > StutterFactor * median) { stutters++; } }

        json.Field("frames", static_cast<u64>(count));
        json.Field("recorded_s", record.RecordedSeconds);
        json.Field("fps_mean", sum > 0.0 ? static_cast<f64>(count) * 1000.0 / sum : 0.0);
        json.Field("frame_ms_mean", count ? sum / static_cast<f64>(count) : 0.0);
        json.Field("frame_ms_p50", median);
        json.Field("frame_ms_p95", Percentile(sorted, 0.95));
        json.Field("frame_ms_p99", Percentile(sorted, 0.99));
        json.Field("frame_ms_p999", Percentile(sorted, 0.999));
        json.Field("frame_ms_max", count ? sorted.back() : 0.0f);
        json.Field("low1_ms", slowest_mean(0.01));
        json.Field("low01_ms", slowest_mean(0.001));
        json.Field("stutters", stutters);
        json.Field("stutter_criterion", "frame_ms > 2 x segment median");
    }

    void WriteSegment(JsonWriter& json, const SegmentRecord& record) {
        const SegmentDesc& desc = record.Desc;
        json.BeginObject();
        json.Field("name", desc.Name);

        json.Key("config");
        json.BeginObject();
        json.Field("path", CameraPathName(desc.Path));
        json.Field("speed", desc.Speed);
        json.Field("speed_unit", desc.Path == CameraPath::Spin ? "deg/s" : "units/s");
        if (desc.Path == CameraPath::SpinMove) { json.Field("spin_rate_deg_s", desc.SpinRate); }
        json.Field("start_yaw_deg", desc.Yaw);
        json.Field("pitch_deg", PitchDegrees);
        json.Field("altitude", GTerrainPass.Config.HeightScale + AltitudeAboveMaxHeight);
        if (desc.Path == CameraPath::Circle) { json.Field("circle_radius", CircleRadius); }
        json.Field("draw_culling", desc.DrawCulling);
        json.Field("generation_culling", desc.GenerationCulling);
        json.Field("props", PropModeName(desc.Props));
        json.Field("duration_s", desc.Duration);
        json.Field("settle_s", record.SettleSeconds);
        json.EndObject();

        json.Field("world_wait_s", record.WorldWaitSeconds);
        json.Field("world_wait_timed_out", record.WorldWaitTimedOut);

        json.Key("summary");
        json.BeginObject();
        WriteFrameSummary(json, record);

        f64 frames = std::max<f64>(static_cast<f64>(record.FrameMs.size()), 1.0);
        u64 generated = 0, evictions = 0;
        for (u32 r = 0; r < RingCount; r++) {
            generated += record.GeneratedAtEnd[r] - record.GeneratedAtStart[r];
            evictions += record.EvictionsAtEnd[r] - record.EvictionsAtStart[r];
        }
        f64 seconds = std::max<f64>(static_cast<f64>(record.RecordedSeconds), 1e-6);

        json.Field("dark_frame_pct", 100.0 * static_cast<f64>(record.DarkFrames) / frames);
        json.Field("dark_mean", record.DarkSum / frames);
        json.Field("dark_max", record.MaxDark);
        json.Field("drawn_mean", record.DrawnSum / frames);
        json.Field("culled_mean", record.CulledSum / frames);
        json.Field("prop_draw_calls_mean", record.PropDrawSum / frames);
        json.Field("chunks_generated", generated);
        json.Field("evictions", evictions);
        json.Field("chunks_per_s", static_cast<f64>(generated) / seconds);
        json.Field("evictions_per_s", static_cast<f64>(evictions) / seconds);
        if (record.WorldCompleteAt >= 0.0f) { json.Field("world_complete_segment_s", record.WorldCompleteAt); }

        json.Field("gpu_samples", record.GpuSamples);
        if (record.GpuSamples > 0) {
            json.Key("gpu_ms_mean");
            json.BeginObject();
            u32 slot = 0;
            auto mean = [&](u32 i) { return record.GpuSums[i] / static_cast<f64>(record.GpuSamples); };
            json.Field("setup", mean(slot++));
            for (const char* name : Renderer::ScenePassNames) { json.Field(name, mean(slot++)); }
            json.Field("resolve", mean(slot++));
            for (const char* name : Renderer::OverlayPassNames) { json.Field(name, mean(slot++)); }
            json.Field("frame", mean(slot++));
            json.EndObject();
        }

        // Generation timing within this segment's window.
        std::vector<f32> compute, latency;
        for (size_t i = record.GenerationLogStart; i < record.GenerationLogEnd; i++) {
            compute.push_back(TerrainManager::GenerationLog[i].ComputeMs);
            latency.push_back(TerrainManager::GenerationLog[i].LatencyMs);
        }
        json.Field("generations", static_cast<u64>(compute.size()));
        if (!compute.empty()) {
            auto mean_of = [](const std::vector<f32>& v) {
                f64 total = 0.0;
                for (f32 x : v) { total += static_cast<f64>(x); }
                return total / static_cast<f64>(v.size());
            };
            json.Field("gen_compute_ms_mean", mean_of(compute));
            json.Field("gen_latency_ms_mean", mean_of(latency));
            std::sort(compute.begin(), compute.end());
            std::sort(latency.begin(), latency.end());
            json.Field("gen_compute_ms_p95", Percentile(compute, 0.95));
            json.Field("gen_latency_ms_p95", Percentile(latency, 0.95));
        }
        json.EndObject();

        if (Scenario.PerFrame) { record.Frames.Write(json, "frames"); }
        record.Ticks.Write(json, "ticks");

        json.EndObject();
    }

    void WriteEngineFields(JsonWriter& json) {
        using namespace TerrainManager;
        json.Key("engine");
        json.BeginObject();
        json.Field("vertices_per_edge", VerticesPerEdge);
        json.Field("ring0_chunk_scale", ChunkScale);
        json.Field("ring0_exploration_radius", ExplorationRadius);
        json.Field("outer_ring_count", OuterRingCount);
        json.Field("outer_ring_exploration_radius", OuterRingExplorationRadius);
        json.Key("outer_ring_chunk_scales");
        json.BeginArray();
        for (f64 scale : OuterRingChunkScales) { json.Float(scale); }
        json.EndArray();
        json.Field("ring0_generation_pool", Ring0GenerationPoolSize);
        json.Field("outer_generation_pool", GenerationPoolSize);
        json.Field("total_max_drawn_chunks", TotalMaxDrawnChunks);
        json.Field("total_max_cached_chunks", TotalMaxCachedChunks);
        json.Field("max_props_per_chunk", MaxPropsPerChunk);
        json.Field("total_coverage_radius", TotalCoverageRadius);
        json.Field("height_scale", GTerrainPass.Config.HeightScale);
        json.Field("max_frames_in_flight", RendererConstants::MAX_FRAMES_IN_FLIGHT);
        json.Field("msaa_samples", static_cast<u32>(RendererConstants::SceneSampleCount));
        json.Field("seed", static_cast<i64>(Generator.Seed));
        json.EndObject();
    }

    void WriteLogFields(JsonWriter& json) {
        json.Key("log");
        json.BeginObject();
        json.Field("warnings", LogCapture ? LogCapture->Warnings : 0u);
        json.Field("errors", LogCapture ? LogCapture->Errors : 0u);
        json.Key("messages");
        json.BeginArray();
        if (LogCapture) {
            for (const std::string& message : LogCapture->Messages) { json.String(message); }
        }
        json.EndArray();
        json.EndObject();
    }

    bool Finish() {
        if (!Active) { return true; }
        WatchdogDisarmed.store(true, std::memory_order_release);

        JsonWriter json;
        json.Out.reserve(1 << 20);
        json.BeginObject();

        if (Scenario.Segments.empty()) {
            json.Field("type", "machine");
            json.Field("commit", INC_GIT_COMMIT);
            json.Field("commit_dirty", INC_GIT_DIRTY != 0);
            json.Field("default_workers", TaskScheduler::WorkerCount());
            SystemInfo::WriteMachineFields(json);
            WriteEngineFields(json);
            WriteLogFields(json);
            json.EndObject();
            return AppendLine(Config.OutPath, json.Out);
        }

        json.Field("type", "run");
        WriteCommonFields(json, Config);
        json.Field("completed", CurrentPhase == Phase::Done);
        json.Field("workers", TaskScheduler::WorkerCount());
        json.Field("logical_threads", std::thread::hardware_concurrency());
        json.Field("width", GRenderer.Swapchain.Width);
        json.Field("height", GRenderer.Swapchain.Height);
        json.Field("uncapped", GEngine.Config.Uncapped);
        json.Field("present_mode", SystemInfo::PresentModeName(VkVault::PresentMode));
        json.Field("gpu_timings", GRenderer.GpuTimings.Available);
        json.Field("per_frame", Scenario.PerFrame);

        json.Key("timeline");
        json.BeginObject();
        json.Field("engine_init_ms", EngineInitMs);
        json.Field("terrain_init_batch_ms", TerrainManager::InitBatchMs);
        json.Field("first_frame_s", FirstFrameSeconds);
        json.Field("world_complete_s", WorldCompleteSeconds);
        json.Field("total_s", SecondsSinceStart());
        json.Field("frames", FrameCount);
        json.EndObject();

        WriteEngineFields(json);

        json.Key("segments");
        json.BeginArray();
        for (const SegmentRecord& record : Segments) { WriteSegment(json, record); }
        json.EndArray();

        // Every generation finalized during the run (warmup included), in finalize order.
        json.Key("generations");
        json.BeginObject();
        {
            std::vector<u32> rings;
            std::vector<f32> compute, latency;
            rings.reserve(TerrainManager::GenerationLog.size());
            compute.reserve(TerrainManager::GenerationLog.size());
            latency.reserve(TerrainManager::GenerationLog.size());
            for (const TerrainManager::GenerationRecord& gen : TerrainManager::GenerationLog) {
                rings.push_back(gen.Ring);
                compute.push_back(gen.ComputeMs);
                latency.push_back(gen.LatencyMs);
            }
            json.Column("ring", rings);
            json.Column("compute_ms", compute, 4);
            json.Column("latency_ms", latency, 4);
        }
        json.EndObject();

        WriteLogFields(json);
        json.EndObject();

        return AppendLine(Config.OutPath, json.Out);
    }
}
