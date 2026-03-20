#include "ImageSystem.hpp"

#include "Utils/ResourcePool.hpp"
#include "Engine/InferusRenderer/VulkanContext.hpp"
#include "Engine/InferusRenderer/RendererConfig.hpp"

namespace ImageSystem {
    namespace View {
        void Create();
        void Destroy();
    }

    ResourcePool<Image> ImagePool;

    void clear(Image* image) {
        if (image->image) { vmaDestroyImage(VulkanContext::VmaAllocator, image->image, image->allocation); }
        image->image = VK_NULL_HANDLE;
    }

    void Create() {
        ImagePool.Reserve(RendererConfig::ImageSystem::RESERVE_CAPACITY);
        View::Create();
    }

    void Destroy() {
        for (Image& image: ImagePool) {
            clear(&image);
        }
        ImagePool.Clear();
        View::Destroy();
    }

    Id add(ImageCreateInfo imageDesc) {
        Image image{};

        VkImageCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        createInfo.mipLevels = 1;
        createInfo.extent.depth = 1;
        createInfo.format = imageDesc.format;
        createInfo.imageType = VK_IMAGE_TYPE_2D;
        createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        createInfo.usage = imageDesc.usage;
        createInfo.extent.width = imageDesc.width;
        createInfo.extent.height = imageDesc.height;
        createInfo.arrayLayers = imageDesc.arrayLayers;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        vmaCreateImage(VulkanContext::VmaAllocator, &createInfo, &allocCreateInfo, &image.image, &image.allocation, nullptr);

        image.width = imageDesc.width;
        image.height = imageDesc.height;
        image.format = imageDesc.format;
        image.mipLevels = imageDesc.mipLevels;
        image.arrayLayers = imageDesc.arrayLayers;
        image.depth = imageDesc.depth;
        image.format = imageDesc.format;
        image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

        return ImagePool.Add(image);
    }

    Image* get(Id id) {
        return ImagePool.Get(id);
    }

    void del(Id id) {
        Image* image = ImagePool.Get(id);
        clear(image);
        ImagePool.Remove(id);
    }

    void upload(Id id, void *upload_data, size_t size) {
        // Mock usage to avoid compiler warnings
        (void)id;
        (void)upload_data;
        (void)size;
    }

    VkImageViewCreateInfo fillDefaultImageViewCreateInfo(Image* image) {
        VkImageViewCreateInfo imageViewCreateInfo {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = image->image;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        imageViewCreateInfo.format = image->format;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = image->mipLevels;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = image->arrayLayers;
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        return imageViewCreateInfo;
    }

    namespace View {
        ResourcePool<ImageView> ViewPool;

        void clear(ImageView* imageView) {
            if (imageView->imageView) {
                vkDestroyImageView(VulkanContext::Device, imageView->imageView, nullptr);
            }
        }

        void Create() {
            ViewPool.Reserve(RendererConfig::ImageSystem::View::RESERVE_CAPACITY);
        }

        void Destroy() {
            for (ImageView& view: ViewPool) {
                clear(&view);
            }
            ViewPool.Clear();
        }

        Id add(VkImageViewCreateInfo& createInfo) {
            ImageView imageView{};

            vkCreateImageView(VulkanContext::Device, &createInfo, nullptr, &imageView.imageView);

            return ViewPool.Add(imageView);
        }

        ImageView* get(Id id) {
            return ViewPool.Get(id);
        }

        void del(Id id) {
            ImageView* view = ViewPool.Get(id);
            clear(view);
            ViewPool.Remove(id);
        }
    };
}
