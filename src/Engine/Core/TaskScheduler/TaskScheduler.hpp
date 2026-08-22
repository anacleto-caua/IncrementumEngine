#pragma once

#include "Definitions.hpp"

namespace TaskScheduler {
    void Create();
    void Destroy();

    void SubmitTask(TaskEntryPoint entry_point, void* payload, TaskPriority priority = TaskPriority::Normal);

    void Wait(std::atomic<u32>& dependency_counter);
}
