#include "StressTest.hpp"

#include <cmath>
#include <vector>
#include <atomic>

#include "TaskScheduler/TaskScheduler.hpp"

namespace TaskScheduler::StressTest {
    // Workload 1: CPU bound trigonometry burner
    struct HeavyMathPayload {
        u32 JobId;
        u32 iterations;
        double DummyResult;
        std::atomic<u32>* PendingCounter;
    };

    void HeavyMathTask(void* raw_payload, [[maybe_unused]]TaskScheduler::WorkerContext& context) {
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

    void MatrixMultiplyTask(void* raw_payload, [[maybe_unused]]TaskScheduler::WorkerContext& context) {
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

    void Run(u32 num_math_jobs, u32 num_math_iterations, u32 matrix_size, u32 chunk_size) {
        TaskScheduler::Create();

        // Heavy CPU Bound Tasks
        std::atomic<u32> math_jobs_pending(num_math_jobs);
        std::vector<HeavyMathPayload> math_payloads(num_math_jobs);

        analog::info("Submitting {} Heavy Math jobs.", num_math_jobs);

        for (u32 i = 0; i < num_math_jobs; ++i) {
            math_payloads[i] = { i, num_math_iterations, 0.0, &math_jobs_pending };
            TaskScheduler::SubmitTask(HeavyMathTask, &math_payloads[i]);
        }

        // Wait for Phase 1 to finish using your scheduler's Wait mechanism
        TaskScheduler::Wait(math_jobs_pending);
        analog::info("Math jobs finished!");

        // Phase 2: Memory Bound Tasks
        u32 num_matrix_jobs = matrix_size / chunk_size;

        std::vector<float> matA(matrix_size * matrix_size, 1.0f);
        std::vector<float> matB(matrix_size * matrix_size, 2.0f);
        std::vector<float> matC(matrix_size * matrix_size, 0.0f);

        std::atomic<u32> matrix_jobs_pending(num_matrix_jobs);
        std::vector<MatrixTaskPayload> matrix_payloads(num_matrix_jobs);

        analog::info("Submitting {} Matrix Multiply jobs ({}x{} matrix).", num_matrix_jobs, matrix_size, matrix_size);

        for (u32 i = 0; i < num_matrix_jobs; ++i) {
            matrix_payloads[i] = {
                i * chunk_size,
                (i + 1) * chunk_size,
                matrix_size,
                &matA,
                &matB,
                &matC,
                &matrix_jobs_pending
            };
            TaskScheduler::SubmitTask(MatrixMultiplyTask, &matrix_payloads[i]);
        }

        TaskScheduler::Wait(matrix_jobs_pending);
        analog::info("Matrix jobs finished! Shutting down.\n");

        TaskScheduler::Destroy();
    }
}
