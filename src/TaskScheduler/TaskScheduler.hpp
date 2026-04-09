#pragma once

namespace TaskScheduler {
    struct WorkerContext {
        u32 ThreadIndex;
        u8* ScratchMemory;
        u64 MemoryHead;

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

    void Create();
    void Destroy();

    void SubmitTask(TaskEntryPoint entry_point, void* payload);
}
