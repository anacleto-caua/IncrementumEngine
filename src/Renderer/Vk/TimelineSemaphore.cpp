#include "TimelineSemaphore.hpp"

#include "Asl/FreeList.hpp"
#include "Renderer/VkVault.hpp"

asl::FreeList<TimelineSemaphoreValue, u16> SemaphoresData;

TimelineSemaphore CreateTimelineSemaphore() {
    TimelineSemaphoreValue semaphore_value = {};
    semaphore_value.LastSignaledValue = 0;

    VkSemaphoreTypeCreateInfo semaphore_type_create_info = {};
    semaphore_type_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphore_type_create_info.pNext = nullptr;
    semaphore_type_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphore_type_create_info.initialValue = 0;

    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.pNext = &semaphore_type_create_info;
    semaphore_create_info.flags = 0;

    VK_OUT(
        vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &semaphore_value.Semaphore),
        "couldn't create a timeline semaphore"
    );

    TimelineSemaphore semaphore_handle;
    semaphore_handle.Index = SemaphoresData.Add(semaphore_value);

    return semaphore_handle;
}

void DestroyTimelineSemaphore(TimelineSemaphore semaphore) {
    TimelineSemaphoreValue* value = GetTimelineSemaphoreValue(semaphore);
    if (value->Semaphore) { vkDestroySemaphore(VkVault::Device, value->Semaphore, nullptr); }
    SemaphoresData.Remove(semaphore.Index);
}

TimelineSemaphoreValue* GetTimelineSemaphoreValue(TimelineSemaphore id) {
    return &SemaphoresData[id.Index];
}

void QueryTimelineSemaphoreValue(TimelineSemaphore semaphore) {
    TimelineSemaphoreValue* value = GetTimelineSemaphoreValue(semaphore);
    vkGetSemaphoreCounterValue(VkVault::Device, value->Semaphore, &value->LastInqueriedValue);
}

void SignalTimelineSemaphore(TimelineSemaphore semaphore, u64 signal_value) {
    TimelineSemaphoreValue* value = GetTimelineSemaphoreValue(semaphore);
    VkSemaphoreSignalInfo signal_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .pNext = nullptr,
        .semaphore = value->Semaphore,
        .value = signal_value
    };
    vkSignalSemaphore(VkVault::Device, &signal_info);

    value->LastSignaledValue = signal_value;
    value->LastInqueriedValue = signal_value;
}

void WaitOnTimelineSemaphore(TimelineSemaphore semaphore, u64 wait_value) {
    TimelineSemaphoreValue* value = GetTimelineSemaphoreValue(semaphore);
    VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext = nullptr,
        .flags = 0,
        .semaphoreCount = 1,
        .pSemaphores = &value->Semaphore,
        .pValues = &wait_value
    };

    vkWaitSemaphores(VkVault::Device, &wait_info, UINT64_MAX);
    value->LastInqueriedValue = wait_value;
}
