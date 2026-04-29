#pragma once

namespace TaskScheduler::StressTest {
    void Run(u32 num_math_jobs, u32 num_math_iterations, u32 matrix_size, u32 chunk_size);
}
