#pragma once

namespace TaskScheduler {
    static constexpr i32 TASK_QUEUE_CAPACITY = 4096;
    static_assert([](){
        i32 n = TASK_QUEUE_CAPACITY;
        i32 x = 2;

        if (n <= 0 || x <= 0) return false;
        if (x == 1) return n == 1;

        int temp = n;
        while (temp % x == 0) {
            temp /= x;
        }

        return temp == 1;

    }() && "TaskScheduler queue capacity should be a power of 2");

    static constexpr i32 TASK_QUEUE_MASK = TASK_QUEUE_CAPACITY - 1;

    struct WorkerContext;
    typedef void (*TaskEntryPoint)(void* payload, WorkerContext& context);

    struct alignas(64) Task {
        TaskEntryPoint EntryPoint = nullptr;
        void* Payload = nullptr;
    };
    static_assert(std::is_trivially_copyable_v<Task>, "Task must be trivially copyable for safe lock-free stealing!");
    static_assert(sizeof(Task) == 64 && "Task must equals a perfect x86_64 cache line to avoid unecessary cross thread cache flush");


    class TaskQueue {
        private:
            Task Tasks[TASK_QUEUE_CAPACITY];

            std::atomic<i32> Top {0};
            std::atomic<i32> Bottom {0};

        public:
            TaskQueue() {};
            ~TaskQueue() {};

            TaskQueue(const TaskQueue&) = delete;
            TaskQueue& operator=(const TaskQueue&) = delete;

            void Push(Task task) {
                i32 b = Bottom.load(std::memory_order_relaxed);
                Tasks[b & TASK_QUEUE_MASK] = task;

                std::atomic_thread_fence(std::memory_order_release);

                Bottom.store(b + 1, std::memory_order_relaxed);
            }

            bool Pop(Task& task) {
                i32 b = Bottom.load(std::memory_order_relaxed) - 1;
                Bottom.store(b, std::memory_order_relaxed);

                // Force memory synchronization so we get the most up-to-date 'top'
                std::atomic_thread_fence(std::memory_order_seq_cst);

                i32 t = Top.load(std::memory_order_relaxed);

                if (t <= b) {
                    // We safely claimed a task
                    task = Tasks[b & TASK_QUEUE_MASK];

                    if (t != b) {
                        // More than 1 task left in the queue. No conflict with thieves.
                        return true;
                    }

                    // Exactly 1 task left. A thief might be trying to steal it right now!
                    // We must race the thief using a Compare-And-Swap (CAS).
                    if (
                            !Top.compare_exchange_strong(
                                t, t + 1,
                                std::memory_order_seq_cst, std::memory_order_relaxed
                            )
                        )
                    {
                        // The thief beat us to it. The queue is empty.
                        return false;
                    }

                    // Reset the queue indices to avoid i32eger overflow over time
                    Bottom.store(b + 1, std::memory_order_relaxed);
                    return true;

                } else {
                    // The queue was already empty. Restore the bottom poi32er.
                    Bottom.store(b + 1, std::memory_order_relaxed);
                    return false;
                }
            }

            bool Steal(Task& task) {
                i32 t = Top.load(std::memory_order_acquire);

                // Force sync to ensure we read bottom after top
                std::atomic_thread_fence(std::memory_order_seq_cst);

                i32 b = Bottom.load(std::memory_order_acquire);

                if (t < b) {
                    // There is at least one Task to steal
                    task = Tasks[t & TASK_QUEUE_MASK];

                    // Attempt to increment the top index. If another thief steals it first,
                    // or the owner pops it first, this CAS will fail.
                    if (
                            Top.compare_exchange_strong(
                                t, t + 1,
                                std::memory_order_seq_cst, std::memory_order_relaxed
                            )
                        )
                    {
                        return true; // We successfully stole the job!
                    }
                }

                // Queue is empty, or we lost the race to another thief
                return false;
            }
    };


    struct WorkerContext {
        u32 ThreadIndex;
        u8* ScratchMemory;
        u64 MemoryHead;
        TaskQueue* Queue;

        void ResetMemory() {
            MemoryHead = 0;
        }
    };
}
