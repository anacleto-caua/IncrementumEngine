#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "Utils/Handle.hpp"

namespace ImageSystem {
    struct ImageCreateInfo {
        uint32_t width;
        uint32_t height;

        uint16_t depth = 1;
        uint8_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    };

    struct Image {
        VkImage image;
        VmaAllocation allocation;

        uint32_t width;
        uint32_t height;
        uint16_t depth;
        uint8_t mipLevels;
        uint32_t arrayLayers;
        VkFormat format;
        VkImageLayout layout;
    };

    using Id = Handle<Image>;

    void Create();
    void Destroy();

    Id add(ImageCreateInfo imageCreateDesc);
    Image* get(Id id);
    void del(Id id);
    void upload(Id id, void *data, size_t size);

    VkImageViewCreateInfo fillDefaultImageViewCreateInfo(Image* image);

    namespace View {
        struct ImageView {
            VkImageView imageView;
        };

        using Id = Handle<ImageView>;

        Id add(VkImageViewCreateInfo& createInfo);
        ImageView* get(Id id);
        void del(Id id);
    };
};
