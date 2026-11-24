#pragma once

#include <chrono>
#include <string>

struct Camera;

// Unattended benchmark runs for the TCC test scenarios (see test_scenerario.md). One process =
// one run of one scenario: the camera follows a scripted path, per-frame and per-tick data is
// recorded in memory, and one JSON Lines record is appended to BenchConfig::OutPath at shutdown.
// Inactive (and every hook a no-op) unless --bench=<scenario> is on the command line.
namespace Benchmark {
    struct BenchConfig {
        std::string Scenario;                   // empty = normal interactive run
        std::string OutPath = "bench_results.jsonl";
        u32 Run = 0;                            // repetition index, recorded only
        std::string WorkersArg;                 // "", "<n>", "half" or "max-1"
        u32 Workers = 0;                        // resolved from WorkersArg, 0 = TaskScheduler default
        u32 Width = 1280;
        u32 Height = 720;
        f32 DurationOverride = -1.0f;           // < 0 = scenario default
        f32 WarmupOverride = -1.0f;
        std::chrono::steady_clock::time_point ProcessStart;
    };

    inline bool Active = false;

    // Parses --key=value arguments. Returns false (and logs) on an unknown scenario or argument.
    bool ParseArgs(i32 argc, char** argv, BenchConfig& out_config);

    // Resolves WorkersArg into Workers. Returns false if this worker count isn't meaningful on this
    // machine (more workers than logical threads) - the caller then calls WriteSkipped() and exits.
    bool ResolveWorkers(BenchConfig& config);
    void WriteSkipped(const BenchConfig& config, const char* reason);

    // Game::Create(), after Engine/TerrainManager init. engine_init_ms is Engine::Init()'s wall time.
    void Init(const BenchConfig& config, Camera& camera, f32 engine_init_ms);

    // Game::Run(), in place of FlyByCamera::Update() (before RefreshChunks) and after the rest of
    // the frame's game-side work respectively.
    void BeginFrame(f32 delta_time);
    void EndFrame();

    // main(), after Game::Run() and before Game::Destroy(): appends this run's record. Returns
    // false if the output file couldn't be written.
    bool Finish();
}
