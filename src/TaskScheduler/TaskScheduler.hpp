#pragma once

#include "TaskScheduler/Definitions.hpp"

namespace TaskScheduler {
    void Create();
    void Destroy();

    void SubmitTask(TaskEntryPoint entry_point, void* payload);

    void Wait(std::atomic<u32>& dependency_counter);
}
