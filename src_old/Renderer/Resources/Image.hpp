#pragma once

#include "Renderer/VkVault.hpp"
#include "Core/ResourcePool.hpp"

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
        QueueContext* OwnerQueue;
    };

    using Id = Handle<Value>;
}
