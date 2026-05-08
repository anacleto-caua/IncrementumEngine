#include "ResourceManager.hpp"

namespace Buffer {
    void Destroy(Value* buffer);
    void DestroyAll();
}

namespace Image {
    void Destroy(Value* buffer);
    void DestroyAll();
}

namespace ImageView {
    void Destroy(Value* buffer);
    void DestroyAll();
}

namespace ResourceManager {
    IncResult Initialize() {
        // ...
        return IncResult::SUCCESS;
    }

    void Shutdown() {
        Buffer::DestroyAll();
        Image::DestroyAll();
        ImageView::DestroyAll();
    }
}

namespace Image {
    asl::ResourcePool<Value> ImagePool;

    Id Add(CreateInfo create_info) {
        Value image {};

        VkImageCreateInfo image_create_info {};
        image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.mipLevels = 1;
        image_create_info.extent.depth = 1;
        image_create_info.format = create_info.Format;
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.usage = create_info.Usage;
        image_create_info.extent.width = create_info.Width;
        image_create_info.extent.height = create_info.Height;
        image_create_info.arrayLayers = create_info.ArrayLayers;

        VmaAllocationCreateInfo alloc_create_info {};
        alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;

        if (
            vmaCreateImage(
                VulkanContext::VmaAllocator,
                &image_create_info, &alloc_create_info,
                &image.Image, &image.Allocation,
                nullptr
            ) != VK_SUCCESS
        ) {
            analog::error("buffer creation failed.");
        }

        image.Width = create_info.Width;
        image.Height = create_info.Height;
        image.Format = create_info.Format;
        image.MipLevels = create_info.MipLevels;
        image.ArrayLayers = create_info.ArrayLayers;
        image.Depth = create_info.Depth;
        image.Format = create_info.Format;
        image.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        return ImagePool.Add(image);
    }

    void Del(Id id) {
        Value* image = Get(id);
        Destroy(image);
        ImagePool.Remove(id);
    }

    Value* Get(Id id) {
        return &ImagePool.Get(id);
    }

    void Destroy(Value* image) {
        if (image->Image) { vmaDestroyImage(VulkanContext::VmaAllocator, image->Image, image->Allocation); }
        image->Image = VK_NULL_HANDLE;
        image->Allocation = VK_NULL_HANDLE;
    }

    void DestroyAll() {
        for (auto image : ImagePool) {
            Destroy(&image);
        }
    }
}

namespace ImageView {
    asl::ResourcePool<Value> ViewPool;

    Id Add(VkImageViewCreateInfo create_info) {
        Value image_view {};

        if (
            vkCreateImageView(
                VulkanContext::Device,
                &create_info,
                nullptr,
                &image_view.ImageView
            ) != VK_SUCCESS
        ) {
            analog::error("buffer creation failed.");
        }

        return ViewPool.Add(image_view);
    }

    void Del(Id id) {
        auto image_view = Get(id);
        Destroy(image_view);
        ViewPool.Remove(id);
    }

    Value* Get(Id id) {
        return &ViewPool.Get(id);
    }

    VkImageViewCreateInfo FillCreateInfo(Image::Value* image) {
        VkImageViewCreateInfo image_view_create_info {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image->Image,
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

    void Destroy(Value* image_view) {
        if (image_view->ImageView) { vkDestroyImageView(VulkanContext::Device, image_view->ImageView, nullptr); }
        image_view->ImageView = VK_NULL_HANDLE;
    }

    void DestroyAll() {
        for(auto image_view : ViewPool) {
            Destroy(&image_view);
        }
    }
}

namespace Buffer {
    asl::ResourcePool<Value> BufferPool;

    Id Add(CreateInfo create_info) {
        Value buffer {};

        VkBufferUsageFlags vk_usage = 0;
        VmaMemoryUsage vma_usage = VMA_MEMORY_USAGE_UNKNOWN;
        VmaAllocationCreateFlags vma_flags = 0;
        VkMemoryPropertyFlags memory_flags = 0;

        switch (create_info.Type) {
            case Type::STAGING:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = 0;
                break;
            case Type::VERTEX:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case Type::INDEX:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case Type::SSBO:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case Type::UBO:
                vk_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            default:
                analog::error("unexpected type on buffer creation.");
                break;
        }

        VkBufferCreateInfo buffer_create_info {};
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size = static_cast<VkDeviceSize>(create_info.Size);
        buffer_create_info.usage = vk_usage;

        VmaAllocationCreateInfo alloc_create_info {};
        alloc_create_info.usage = vma_usage;
        alloc_create_info.requiredFlags = memory_flags;
        alloc_create_info.flags = vma_flags;

        if (
            vmaCreateBuffer(
                VulkanContext::VmaAllocator,
                &buffer_create_info, &alloc_create_info,
                &buffer.Buffer, &buffer.Allocation,
                nullptr
            ) != VK_SUCCESS
        ) {
            analog::error("buffer creation failed.");
        }

        buffer.Size = create_info.Size;
        buffer.Type = create_info.Type;

        return BufferPool.Add(buffer);
    };

    void Del(Id id) {
        Value* buffer = Get(id);
        if (buffer) {
            Destroy(buffer);
            BufferPool.Remove(id);
        }
    }

    Value* Get(Id id) {
        return &BufferPool.Get(id);
    }

    void Destroy(Value* buffer) {
        if (buffer->Buffer) { vmaDestroyBuffer(VulkanContext::VmaAllocator, buffer->Buffer, buffer->Allocation); }
        buffer->Buffer = VK_NULL_HANDLE;
        buffer->Allocation = VK_NULL_HANDLE;
    }

    void DestroyAll() {
        for(auto buffer : BufferPool) {
            Destroy(&buffer);
        }
    }
}
