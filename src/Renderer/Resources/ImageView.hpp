#pragma once

#include "Image.hpp"
#include "Core/ResourcePool.hpp"

struct ImageView {
    VkImageView Handle = VK_NULL_HANDLE;
};

using ImageViewId = Handle<ImageView>;

struct ImageViewPool {
    IncResult Add(VkImageViewCreateInfo create_info, ImageViewId& out_id) {
        ImageView image_view {};

        VK_CHECK(
            vkCreateImageView(
                VkVault::Device,
                &create_info,
                nullptr,
                &image_view.Handle
            ),
            "image view creation failed."
        );

        out_id = Pool.Add(image_view);
        return IncResult::SUCCESS;
    }

    void Del(ImageViewId id) {
        ImageView* image_view = Get(id);
        Destroy(image_view);
        Pool.Remove(id);
    }

    ImageView* Get(ImageViewId id) {
        return &Pool.Get(id);
    }

    void DestroyAll() {
        for (auto image_view : Pool) {
            Destroy(&image_view);
        }
    }

private:
    ResourcePool<ImageView> Pool;

    void Destroy(ImageView* image_view) {
        if (image_view->Handle) { vkDestroyImageView(VkVault::Device, image_view->Handle, nullptr); }
        image_view->Handle = VK_NULL_HANDLE;
    }
};

inline ImageViewPool ImageViews;

// Pure helper, not pool state - fills out a sensible default 2D-array image view create info
// for "image", which callers can still tweak (e.g. DepthBuffer overrides subresourceRange).
inline VkImageViewCreateInfo FillImageViewCreateInfo(Image* image) {
    VkImageViewCreateInfo image_view_create_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image->Handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = image->Format,
        .components {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = image->MipLevels,
            .baseArrayLayer = 0,
            .layerCount = image->ArrayLayers
        }
    };

    return image_view_create_info;
}
