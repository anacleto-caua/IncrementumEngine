#pragma once

#include "Definitions.hpp"

namespace TaskScheduler {
    // num_threads == 0 picks the default (half the logical processors - see Create()).
    void Create(u32 num_threads = 0);
    u32 WorkerCount();
    void Destroy();

    void SubmitTask(TaskEntryPoint entry_point, void* payload, TaskPriority priority = TaskPriority::Normal);

    void Wait(std::atomic<u32>& dependency_counter);
}
