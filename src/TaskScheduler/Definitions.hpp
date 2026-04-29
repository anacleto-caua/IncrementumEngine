#pragma once

#include "concurrentqueue.h"

namespace TaskScheduler {
    class TaskQueue;
    struct WorkerContext;

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

    struct WorkerContext {
        u32 ThreadIndex;
        u8* ScratchMemory;
        u64 MemoryHead;
        TaskQueue* Queue;

        void ResetMemory() {
            MemoryHead = 0;
        }
    };

    typedef void (*TaskEntryPoint)(void* payload, WorkerContext& context);
    struct alignas(64) Task {
        TaskEntryPoint EntryPoint = nullptr;
        void* Payload = nullptr;
    };

    static_assert(std::is_trivially_copyable_v<Task>, "Task must be trivially copyable for safe lock-free stealing!");
    static_assert(sizeof(Task) == 64 && "Task must equals a perfect x86_64 cache line to avoid unecessary cross thread cache flush");

    class TaskQueue {
        private:
            moodycamel::ConcurrentQueue<Task> Queue;

            moodycamel::ProducerToken ProducerToken;
            moodycamel::ConsumerToken ConsumerToken;

        public:
            TaskQueue() : ProducerToken(Queue), ConsumerToken(Queue) {}

            TaskQueue(const TaskQueue&) = delete;
            TaskQueue& operator=(const TaskQueue&) = delete;

            // --- Owner thread methods - not thread safe
            void Push(Task task) {
                Queue.enqueue(ProducerToken, std::move(task));
            }

            bool Pop(Task& task) {
                return Queue.try_dequeue(ConsumerToken, task);
            }

            // --- External threads methods - thread safe
            bool Steal(Task& task) {
                return Queue.try_dequeue(task);
            }

            void PushExternal(Task task) {
                Queue.enqueue(std::move(task));
            }

            size_t SizeApprox() const {
                return Queue.size_approx();
            }
    };
}
