#include "ImageSystem.hpp"

#include <vector>
#include <cstdint>

#include "Engine/InferusRenderer/VulkanContext.hpp"
#include "Engine/InferusRenderer/RendererConfig.hpp"

namespace ImageSystem {
    namespace View {
        void Create();
        void Destroy();
    }

    std::vector<Image> Data;
    std::vector<Id> FreeIndices;

    void clear(Image& image) {
        if (image.image) { vmaDestroyImage(VulkanContext::VmaAllocator, image.image, image.allocation); }
        image.image = VK_NULL_HANDLE;
    }

    void Create() {
        Data.clear();
        Data.reserve(RendererConfig::ImageSystem::DATA_RESERVE_CAPACITY);
        FreeIndices.clear();
        FreeIndices.reserve(RendererConfig::ImageSystem::FREE_INDICES_RESERVE_CAPACITY);
        View::Create();
    }

    void Destroy() {
        for (auto image: Data) {
            clear(image);
        }
        View::Destroy();
    }

    Id add(ImageCreateInfo imageDesc) {
        Id id;
        Image image{};

        if(FreeIndices.empty()) {
            id = { .index = (uint32_t)Data.size() };
            Data.resize(Data.size() + 1);
        } else {
            id = { FreeIndices.back() };
            FreeIndices.pop_back();
        }

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

        Data[id.index] = image;
        return id;
    }

    Image& get(Id id) {
        return Data[id.index];
    }

    void del(Id id) {
        clear(Data[id.index]);
        FreeIndices.push_back(id);
    }

    void upload(Id id, void *upload_data, size_t size) {
        // Mock usage to avoid compiler warnings
        (void)id;
        (void)upload_data;
        (void)size;
    }

    VkImageViewCreateInfo fillDefaultImageViewCreateInfo(Image& image) {
        VkImageViewCreateInfo imageViewCreateInfo {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = image.image;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        imageViewCreateInfo.format = image.format;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = image.mipLevels;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = image.arrayLayers;
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        return imageViewCreateInfo;
    }

    namespace View {
        std::vector<ImageView> Data;
        std::vector<Id> FreeIndices;

        void clear(ImageView imageView) {
            if (imageView.imageView) {
                vkDestroyImageView(VulkanContext::Device, imageView.imageView, nullptr);
            }
        }

        void Create() {
            Data.clear();
            Data.reserve(RendererConfig::ImageSystem::DATA_RESERVE_CAPACITY);
            FreeIndices.clear();
            FreeIndices.reserve(RendererConfig::ImageSystem::FREE_INDICES_RESERVE_CAPACITY);
        }

        void Destroy() {
            for (auto image: Data) {
                clear(image);
            }
        }

        Id add(VkImageViewCreateInfo& createInfo) {
            ImageView imageView{};
            Id id;

            if(FreeIndices.empty()) {
                id = { .index = (uint32_t)Data.size() };
                Data.resize(Data.size() + 1);
            } else {
                id = { FreeIndices.back() };
                FreeIndices.pop_back();
            }

            vkCreateImageView(VulkanContext::Device, &createInfo, nullptr, &imageView.imageView);

            Data[id.index] = imageView;
            return id;
        }

        ImageView& get(Id id) {
            return Data[id.index];
        }

        void del(Id id) {
            clear(Data[id.index]);
            FreeIndices.push_back(id);
        }
    };
}
