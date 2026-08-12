#pragma once

#include <vector>

#include <vulkan/vulkan.h>

class SpecializationBuilder {
private:
    std::vector<VkSpecializationMapEntry> entries;
    std::vector<char> data_buffer;
    VkSpecializationInfo info {};

public:
    template<typename T>
    SpecializationBuilder& AddConstant(u32 constant_id, T value) {
        u32 current_offset = static_cast<u32>(data_buffer.size());

        entries.push_back({constant_id, current_offset, sizeof(T)});

        const char* bytes = reinterpret_cast<const char*>(&value);
        data_buffer.insert(data_buffer.end(), bytes, bytes + sizeof(T));

        return *this;
    }

    const VkSpecializationInfo* Build() {
        info = {
            .mapEntryCount = static_cast<u32>(entries.size()),
            .pMapEntries = entries.data(),
            .dataSize = data_buffer.size(),
            .pData = data_buffer.data()
        };

        return &info;
    }
};
