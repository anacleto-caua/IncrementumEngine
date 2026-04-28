#include  "TaskScheduler.hpp"

#include <deque>
#include <vector>
#include <thread>
#include <cassert>

static constexpr u64 THREAD_SCRATCH_MEMORY_SIZE = 1024 * 1024;

namespace TaskScheduler {
    std::atomic<bool> StopSystem = false;

    u8 NumThreads;
    std::vector<std::thread> Workers;
    std::vector<WorkerContext> WorkerContexts;
    std::deque<TaskQueue> WorkersTaskQueues;

    void WorkerThreadLoop(u32 thread_index) {
        WorkerContext& context = WorkerContexts[thread_index];
        TaskQueue* queue = context.Queue;

        while (!StopSystem) {
            Task current_task;
            bool has_task = false;

            // Try to pop a task from the self queue
            has_task = queue->Pop(current_task);

            // No self task, gotta steal
            if (!has_task) {
                // Pick a random thread to steal from
                // TODO: A better approach would be using a fast thread-local xor-shift RNG
                for (u32 i = 0; i < NumThreads; ++i) {
                    u32 victim_index = (thread_index + i) % NumThreads;
                    // TODO: This looks ugly huh
                    if (victim_index == thread_index) continue;

                    has_task = WorkersTaskQueues[victim_index].Steal(current_task);
                    if (has_task) {
                        break; // Successfully stole a task!
                    }
                }
            }

            // Execute the task
            if (has_task && current_task.EntryPoint) {
                current_task.EntryPoint(current_task.Payload, context);
                // TODO: Reset linear allocator here if task is fully done
                context.ResetMemory(); // Safe as long as data doesn't outlive the task
            } else {
                // TODO: Study for a better idea
                // Yield or Sleep if there is absolutely no work
                std::this_thread::yield();
            }
        }
    }

    void Create() {
        // Leave one thread for the main thread to avoid OS overhead
        NumThreads = std::thread::hardware_concurrency() - 1;
        if (NumThreads == 0) NumThreads = 1;

        WorkerContexts.resize(NumThreads);
        WorkersTaskQueues.resize(NumThreads);

        for (uint32_t i = 0; i < NumThreads; ++i) {
            WorkerContexts[i] = {
                .ThreadIndex = i,
                .ScratchMemory = new u8[THREAD_SCRATCH_MEMORY_SIZE],
                .MemoryHead = 0,
                .Queue = &WorkersTaskQueues[i],
            };

            Workers.emplace_back(&TaskScheduler::WorkerThreadLoop, i);
        }
        analog::info("Task System initialized with {} worker threads.\n",  NumThreads);
    }

    void Destroy() {
        // Signal all threads to stop
        StopSystem.store(true, std::memory_order_release);

        // Wait for all threads to finish their current loop
        for (auto& worker : Workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        // Free the scratch memory
        for (auto& ctx : WorkerContexts) {
            delete[] ctx.ScratchMemory;
        }
    }

    // TODO: Give the main thread a Single Producer Multiple Consumer Queue,
    // this approach won't be usefull for workers spawning their own tasks
    void SubmitTask(TaskEntryPoint entry_point, void* payload) {
        // Basic Round-Robin distribution from the Main Thread
        static std::atomic<u32> next_queue{0};
        u32 index = next_queue.fetch_add(1, std::memory_order_relaxed) % NumThreads;

        WorkersTaskQueues[index].Push({entry_point, payload});
    }

    // TODO: Completely rethink this
    void Wait(std::atomic<u32>& dependency_counter) {
        // Create a dummy context for the main thread if tasks require it
        WorkerContext dummy_context{ .ThreadIndex = 999, .ScratchMemory = nullptr, .MemoryHead = 0, .Queue = nullptr };

        while (dependency_counter.load(std::memory_order_acquire) > 0) {
            Task current_task;
            bool has_task = false;

            // Main thread doesn't have its own queue to Pop from, so it instantly becomes a thief
            for (u32 i = 0; i < NumThreads; ++i) {
                has_task = WorkersTaskQueues[i].Steal(current_task);
                if (has_task) break;
            }

            if (has_task && current_task.EntryPoint) {
                current_task.EntryPoint(current_task.Payload, dummy_context);
            } else {
                std::this_thread::yield();
            }
        }
    }
};
