#pragma once

#include "Renderer/VkVault.hpp"
#include "Core/ResourcePool.hpp"

struct Image {
    struct CreateInfo {
        u32 Width;
        u32 Height;
        u16 Depth = 1;
        u8 MipLevels = 1;
        u32 ArrayLayers = 1;
        VkFormat Format = VK_FORMAT_R8G8B8A8_SRGB;
        VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        QueueRole OwnerQueue = QueueRole::Transfer;
        VkImageLayout UsageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    VkImage Handle = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    u32 Width = 0;
    u32 Height = 0;
    u16 Depth = 0;
    u8 MipLevels = 0;
    u32 ArrayLayers = 0;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageLayout UsageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    QueueRole OwnerQueue = QueueRole::Transfer;
};

using ImageId = Handle<Image>;

struct ImagePool {
    IncResult Add(Image::CreateInfo create_info, ImageId& out_id) {
        Image image {};

        VkImageCreateInfo image_create_info {};
        image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.mipLevels = 1;
        image_create_info.extent.depth = 1;
        image_create_info.format = create_info.Format;
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.usage = create_info.Usage;
        image_create_info.extent.width = create_info.Width;
        image_create_info.extent.height = create_info.Height;
        image_create_info.arrayLayers = create_info.ArrayLayers;

        VmaAllocationCreateInfo alloc_create_info {};
        alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;

        VK_CHECK(
            vmaCreateImage(
                VkVault::VmaAllocator,
                &image_create_info, &alloc_create_info,
                &image.Handle, &image.Allocation,
                nullptr
            ),
            "image creation failed."
        );

        image.Width = create_info.Width;
        image.Height = create_info.Height;
        image.Format = create_info.Format;
        image.MipLevels = create_info.MipLevels;
        image.ArrayLayers = create_info.ArrayLayers;
        image.Depth = create_info.Depth;
        image.Format = create_info.Format;
        image.UsageLayout = create_info.UsageLayout;
        image.OwnerQueue = create_info.OwnerQueue;

        out_id = Pool.Add(image);
        return IncResult::SUCCESS;
    }

    void Del(ImageId id) {
        Image* image = Get(id);
        Destroy(image);
        Pool.Remove(id);
    }

    Image* Get(ImageId id) {
        return &Pool.Get(id);
    }

    void DestroyAll() {
        for (auto image : Pool) {
            Destroy(&image);
        }
    }

private:
    ResourcePool<Image> Pool;

    void Destroy(Image* image) {
        if (image->Handle) { vmaDestroyImage(VkVault::VmaAllocator, image->Handle, image->Allocation); }
        image->Handle = VK_NULL_HANDLE;
        image->Allocation = VK_NULL_HANDLE;
    }
};

inline ImagePool Images;
