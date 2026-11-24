#include <chrono>

#include "Game/Game.hpp"
#include "Game/Benchmark/Benchmark.hpp"

int main(int argc, char** argv) {
    // First thing, so a benchmark's cold-start timeline covers the whole process lifetime.
    auto process_start = std::chrono::steady_clock::now();

    analog::init();

    Benchmark::BenchConfig bench_config;
    bench_config.ProcessStart = process_start;
    if (!Benchmark::ParseArgs(argc, argv, bench_config)) {
        return 1;
    }

    if (Benchmark::Active && !Benchmark::ResolveWorkers(bench_config)) {
        Benchmark::WriteSkipped(bench_config, "worker count exceeds logical threads - 1");
        return 0;
    }

    if (Game::Create(bench_config) != IncResult::SUCCESS) {
        analog::error("couldn't create game");
        return -1;
    };

    Game::Run();

    // Before teardown, so a crash during shutdown can't lose the run's data.
    i32 exit_code = Benchmark::Finish() ? 0 : 2;

    Game::Destroy();

    return exit_code;
}
