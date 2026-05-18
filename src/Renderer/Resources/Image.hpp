#pragma once

#include "Renderer/Vk.hpp"
#include "Asl/ResourcePool.hpp"

namespace Image {
    struct Value {
        VkImage Image;
        VmaAllocation Allocation;
        u32 Width;
        u32 Height;
        u16 Depth;
        u8 MipLevels;
        u32 ArrayLayers;
        VkFormat Format;
        VkImageLayout Layout;
        QueueContext* OwnerQueue = &VulkanContext::Transfer;
    };

    using Id = asl::Handle<Value>;
}
