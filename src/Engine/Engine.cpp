#include "Engine.hpp"

#include "TaskScheduler/TaskScheduler.hpp"

namespace Engine {

    IncResult Create() {
        TaskScheduler::Create();
        return IncResult::SUCCESS;
    }

    void Run() {}

    void Destroy() {
       TaskScheduler::Destroy();
    }
}
