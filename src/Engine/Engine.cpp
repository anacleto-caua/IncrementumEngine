#include "Engine.hpp"

#include "TaskScheduler/TaskScheduler.hpp"

// test
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

struct TestPayload {
    int jobID;
    std::atomic<int>* pendingCounter;
};

void PrintTask(void* rawPayload, TaskScheduler::WorkerContext& context) {
    TestPayload* payload = static_cast<TestPayload*>(rawPayload);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    analog::info("Task {} executed by Thread: {}.", payload->jobID, context.ThreadIndex);

    payload->pendingCounter->fetch_sub(1, std::memory_order_release);
}

namespace Engine {

    IncResult Create() {
        TaskScheduler::Create();

        const int NUM_TEST_JOBS = 20;
        std::atomic<int> jobsPending(NUM_TEST_JOBS);
        std::vector<TestPayload> payloads(NUM_TEST_JOBS);

        analog::info("Submitting {} jobs.", NUM_TEST_JOBS);

        for (int i = 0; i < NUM_TEST_JOBS; ++i) {
            payloads[i] = { i, &jobsPending };
            TaskScheduler::SubmitTask(PrintTask, &payloads[i]);
        }

        while (jobsPending.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }

        analog::info("All jobs finished! Shutting down.\n");

        return IncResult::SUCCESS;
    }

    void Run() {}

    void Destroy() {
        TaskScheduler::Destroy();
    }
}
