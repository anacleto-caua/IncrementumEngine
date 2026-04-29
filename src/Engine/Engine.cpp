#include "Engine.hpp"

#include "TaskScheduler/TaskScheduler.hpp"
#include "TaskScheduler/Test/StressTest.hpp"

namespace Engine {

    IncResult Create() {
        TaskScheduler::StressTest::Run(100, 50, 100, 16);
        return IncResult::SUCCESS;
    }

    void Run() {}

    void Destroy() {
       //TaskScheduler::Destroy();
    }
}
