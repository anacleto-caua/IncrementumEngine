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

    struct Task {
        TaskEntryPoint EntryPoint = nullptr;
        void* Payload = nullptr;
    };

    void Create();
    void Destroy();

    void SubmitTask(TaskEntryPoint entry_point, void* payload);
}
