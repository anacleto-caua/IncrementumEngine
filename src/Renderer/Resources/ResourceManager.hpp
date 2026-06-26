#pragma once

#include "Image.hpp"
#include "Buffer.hpp"
#include "ImageView.hpp"

namespace ResourceManager {
    IncResult Initialize();
    void Shutdown();
}

namespace Buffer {
    struct CreateInfo {
        size_t Size;
        Type Type;
    };

    Id Add(CreateInfo create_info);
    void Del(Id id);

    Value* Get(Id id);

    void* Map(const VmaAllocation alloc);
    void Unmap(const VmaAllocation alloc);
}

namespace Image {
    struct CreateInfo {
        u32 Width;
        u32 Height;
        u16 Depth = 1;
        u8 MipLevels = 1;
        u32 ArrayLayers = 1;
        VkFormat Format = VK_FORMAT_R8G8B8A8_SRGB;
        VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        QueueContext* OwnerQueue;
    };

    Id Add(CreateInfo create_info);
    void Del(Id id);
    Value* Get(Id id);
}

namespace ImageView {
    Id Add(VkImageViewCreateInfo create_info);
    void Del(Id id);
    Value* Get(Id id);

    VkImageViewCreateInfo FillCreateInfo(Image::Value* image);
}

