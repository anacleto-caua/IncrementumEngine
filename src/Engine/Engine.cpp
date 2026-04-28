#include "Engine.hpp"
#include "TaskScheduler/TaskScheduler.hpp"

#include <vector>
#include <atomic>
#include <cmath>

// Workload 1: CPU bound trigonometry burner
struct HeavyMathPayload {
    u32 JobId;
    u32 iterations;
    double DummyResult;
    std::atomic<u32>* PendingCounter;
};

void HeavyMathTask(void* raw_payload, TaskScheduler::WorkerContext& context) {
    HeavyMathPayload* payload = static_cast<HeavyMathPayload*>(raw_payload);

    double accumulator = 0.0;
    for (u32 i = 1; i <= payload->iterations; ++i) {
        accumulator += std::sin(i * 0.01) * std::cos(i * 0.02) + std::sqrt(i);
    }

    payload->DummyResult = accumulator;
    payload->PendingCounter->fetch_sub(1, std::memory_order_release);
}

// Workload 2: Memory/Cache bound matrix math
struct MatrixTaskPayload {
    u32 StartRow;
    u32 EndRow;
    u32 N;
    const std::vector<float>* A;
    const std::vector<float>* B;
    std::vector<float>* C;
    std::atomic<u32>* PendingCounter;
};

void MatrixMultiplyTask(void* raw_payload, TaskScheduler::WorkerContext& context) {
    MatrixTaskPayload* payload = static_cast<MatrixTaskPayload*>(raw_payload);

    u32 N = payload->N;
    const auto& A = *(payload->A);
    const auto& B = *(payload->B);
    auto& C = *(payload->C);

    for (u32 i = payload->StartRow; i < payload->EndRow; ++i) {
        for (u32 j = 0; j < N; ++j) {
            float sum = 0.0f;
            for (u32 k = 0; k < N; ++k) {
                sum += A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }

    payload->PendingCounter->fetch_sub(1, std::memory_order_release);
}

namespace Engine {

    IncResult Create() {
        TaskScheduler::Create();

        // Heavy CPU Bound Tasks
        constexpr u32 NUM_MATH_JOBS = 10;
        constexpr u32 MATH_ITERATIONS = 500;
        std::atomic<u32> math_jobs_pending(NUM_MATH_JOBS);
        std::vector<HeavyMathPayload> math_payloads(NUM_MATH_JOBS);

        analog::info("Submitting {} Heavy Math jobs.", NUM_MATH_JOBS);

        for (u32 i = 0; i < NUM_MATH_JOBS; ++i) {
            math_payloads[i] = { i, MATH_ITERATIONS, 0.0, &math_jobs_pending };
            TaskScheduler::SubmitTask(HeavyMathTask, &math_payloads[i]);
        }

        // Wait for Phase 1 to finish using your scheduler's Wait mechanism
        TaskScheduler::Wait(math_jobs_pending);
        analog::info("Math jobs finished!");

        // Phase 2: Memory Bound Tasks
        constexpr u32 MATRIX_SIZE = 100;
        constexpr u32 CHUNK_SIZE = 32;
        constexpr u32 NUM_MATRIX_JOBS = MATRIX_SIZE / CHUNK_SIZE;

        std::vector<float> matA(MATRIX_SIZE * MATRIX_SIZE, 1.0f);
        std::vector<float> matB(MATRIX_SIZE * MATRIX_SIZE, 2.0f);
        std::vector<float> matC(MATRIX_SIZE * MATRIX_SIZE, 0.0f);

        std::atomic<u32> matrix_jobs_pending(NUM_MATRIX_JOBS);
        std::vector<MatrixTaskPayload> matrix_payloads(NUM_MATRIX_JOBS);

        analog::info("Submitting {} Matrix Multiply jobs ({}x{} matrix).", NUM_MATRIX_JOBS, MATRIX_SIZE, MATRIX_SIZE);

        for (u32 i = 0; i < NUM_MATRIX_JOBS; ++i) {
            matrix_payloads[i] = {
                i * CHUNK_SIZE,
                (i + 1) * CHUNK_SIZE,
                MATRIX_SIZE,
                &matA,
                &matB,
                &matC,
                &matrix_jobs_pending
            };
            TaskScheduler::SubmitTask(MatrixMultiplyTask, &matrix_payloads[i]);
        }

        TaskScheduler::Wait(matrix_jobs_pending);
        analog::info("Matrix jobs finished! Shutting down.\n");

        return IncResult::SUCCESS;
    }

    void Run() {}

    void Destroy() {
        TaskScheduler::Destroy();
    }
}
