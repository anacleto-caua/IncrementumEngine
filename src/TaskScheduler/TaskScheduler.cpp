#include  "TaskScheduler.hpp"

#include <deque>
#include <mutex>
#include <vector>
#include <thread>
#include <cassert>
#include <condition_variable>

static constexpr u64 THREAD_SCRATCH_MEMORY_SIZE = 1024 * 1024;

namespace TaskScheduler {
    u8 NumThreads;
    std::vector<std::thread> Workers;
    std::vector<WorkerContext> WorkerContexts;
    std::deque<TaskQueue> WorkersTaskQueues;

    /*
    // The Task Queue is a basic locked queue for simplicity.
    // A more professional approach would be a lock free ring buffer or some other queue for work stealing
    std::vector<Task> TaskQueue;
    std::mutex QueueMutex;
    std::condition_variable Condition;
    bool StopSystem = false;
    */

    void WorkerThreadLoop(u32 thread_index) {
        WorkerContext& context = WorkerContexts[thread_index];

        /*
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(QueueMutex);
                Condition.wait(lock, []{
                    return !TaskQueue.empty() || StopSystem;
                });

                if (StopSystem && TaskQueue.empty()) {
                    return;
                }

                task = TaskQueue.back();
                TaskQueue.pop_back();
            }

            // Execute the task
            if (task.EntryPoint) {
                task.EntryPoint(task.Payload, context);
            }
        }
        */
    }

    void Create() {
        // Leave one thread for the main thread to avoid (OS overhead)
        NumThreads = std::thread::hardware_concurrency() - 1;
        if (NumThreads == 0) NumThreads = 1;

        WorkerContexts.resize(NumThreads);
        WorkersTaskQueues.resize(NumThreads);

        for (uint32_t i = 0; i < NumThreads; ++i) {
            WorkerContexts[i] = {
                .ThreadIndex = i,
                .ScratchMemory = new u8[THREAD_SCRATCH_MEMORY_SIZE],
                .MemoryHead = 0,
                .Queue = nullptr,
            };

            Workers.emplace_back(&TaskScheduler::WorkerThreadLoop, i);
        }
        analog::info("Task System initialized with {} worker threads.\n",  NumThreads);
    }

    void Destroy() {
        /*
        {
            std::unique_lock<std::mutex> lock(QueueMutex);
            StopSystem = true;
        }
        Condition.notify_all();

        for (auto& worker : Workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        for (auto& ctx : WorkerContexts) {
            delete[] ctx.ScratchMemory;
        }
        */
    }

    void SubmitTask(TaskEntryPoint entry_point, void* payload) {
        /*
        {
            std::unique_lock<std::mutex> lock(QueueMutex);
            TaskQueue.push_back({entry_point, payload});
        }
        Condition.notify_one();
        */
    }
};
