#pragma once

#include <atomic>

#include "TaskScheduler/TaskScheduler.hpp"

namespace TaskScheduler {
    static constexpr i32 CAPACITY = 4096;

    static constexpr i32 MASK = CAPACITY - 1;

    class TaskQueue {
        private:
            Task Tasks[CAPACITY];

            std::atomic<i32> Top {0};
            std::atomic<i32> Bottom {0};

        public:
            void Push(Task task) {
                i32 b = Bottom.load(std::memory_order_relaxed);
                Tasks[b & MASK] = task;

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
                    task = Tasks[b & MASK];

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
                    task = Tasks[t & MASK];

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
}
