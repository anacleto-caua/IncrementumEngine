#include "Renderer.hpp"

#include <array>
#include <cstdint>
#include <algorithm>

#include <spdlog/spdlog.h>

#include "Engine/Core/Window.hpp"
#include "Engine/InferusRenderer/Recipes.hpp"
#include "Engine/InferusRenderer/VulkanContext.hpp"
#include "Engine/InferusRenderer/RendererConfig.hpp"
#include "Engine/InferusRenderer/Image/ImageSystem.hpp"
#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"
#include "Engine/InferusRenderer/Passes/ImGuiRenderer.hpp"
#include "Engine/InferusRenderer/Passes/TerrainRenderer.hpp"

namespace Renderer {
    struct FrameData {
        float DeltaTime = 0;
        VkFence InFlight = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        VkCommandPool CmdPool = VK_NULL_HANDLE;
        VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
    };

    struct SwapchainImage {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView ImageView = VK_NULL_HANDLE;
        VkSemaphore RenderFinished = VK_NULL_HANDLE;
    };

    static constexpr size_t CREATION_WISE_STAGING_BUFFER_SIZE = 1 * 1024 * 1024;
    // Swapchain
    VkSwapchainCreateInfoKHR SwapchainCreateInfo {};

    VkExtent2D Extent;
    VkSwapchainKHR Swapchain;

    VkPresentInfoKHR PresentInfo {};
    std::vector<SwapchainImage> SwapchainImages;

    // Per frame data
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> Frames;
    uint32_t TargetFrameIndex = 0;
    uint32_t TargetImageViewIndex = 0;

    // Drawing -- I imagine this may be shared between all the other pipelines
    static constexpr VkPipelineStageFlags G_PIPELINE_WAIT_STAGES[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkRect2D Scissor {};
    VkViewport Viewport {};
    VkRenderingAttachmentInfo ColorAttachment {};
    VkRenderingAttachmentInfo DepthAttachment {};
    VkRenderingInfo RenderingInfo {};

    VkCommandBufferBeginInfo PipelineCmdBeginInfo {};
    VkSubmitInfo PipelineCmdSubmitInfo {};

    void RefreshExtent();
    void DestroySwapchain(VkSwapchainKHR OldSwapchain);
    void RecreateSwapchain(VkSwapchainKHR OldSwapchain);
    void CleanupSwapchainImages();
    void QuerySurfaceCapabilities();

    namespace DepthBuffer {
        ImageSystem::Id Image;
        ImageSystem::View::Id ImageView;

        VkClearDepthStencilValue ClearStencilValue = {
            .depth = 0.0,
            .stencil = 1
        };

        VkImageSubresourceRange Range = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        void Create(uint32_t Width, uint32_t Height) {
            VkCommandBuffer cmd = VulkanContext::SingleTimeCmdBegin(VulkanContext::Graphics);

            ImageSystem::ImageCreateInfo DepthBufferDesc {};
            DepthBufferDesc.width = Width;
            DepthBufferDesc.height = Height;
            DepthBufferDesc.format = RendererConfig::DepthBuffer::Format;
            DepthBufferDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            Image = ImageSystem::add(DepthBufferDesc);
            ImageSystem::Image DepthImage = ImageSystem::get(DepthBuffer::Image);
            DepthImage.format = RendererConfig::DepthBuffer::Format;

            // Despite having a creation format the image still starts as a _UNDEFINED, so transit it a first time
            VkImageMemoryBarrier barrier;
            barrier = Recipes::ImageMemoryBarrier::DepthBuffer::MakeValid(DepthImage, DepthBuffer::Range);
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

            vkCmdPipelineBarrier(
                cmd,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            VulkanContext::SingleTimeCmdSubmit(VulkanContext::Graphics, cmd);

            VkImageViewCreateInfo ImageViewCreateInfo = ImageSystem::fillDefaultImageViewCreateInfo(DepthImage);
            ImageViewCreateInfo.subresourceRange = DepthBuffer::Range;

            ImageView = ImageSystem::View::add(ImageViewCreateInfo);
        }

        void Recreate(uint32_t Width, uint32_t Height) {
            ImageSystem::del(Image);
            ImageSystem::View::del(ImageView);
            Create(Width, Height);
        }
    }

    InferusResult Create() {
        VulkanContext::Create();
        // Memory resources management systems
        BufferSystem::Create();
        BufferSystem::Id CreationWiseStagingBuffer;
        {
            BufferSystem::CreateInfo CreationWiseStagingBufferCreateDesc = {
                .size = CREATION_WISE_STAGING_BUFFER_SIZE,
                .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
                .usage = BufferSystem::CreateInfoUsage::STAGING
            };
            CreationWiseStagingBuffer = BufferSystem::add(CreationWiseStagingBufferCreateDesc);
        }
        ImageSystem::Create();

        QuerySurfaceCapabilities();
        Extent = SurfaceCapabilities.currentExtent;
        SwapchainImageCount = SurfaceCapabilities.minImageCount + 1;

        SwapchainCreateInfo = {};
        SwapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        SwapchainCreateInfo.surface = VulkanContext::Surface;
        SwapchainCreateInfo.minImageCount = SwapchainImageCount;
        SwapchainCreateInfo.imageFormat = VulkanContext::SurfaceFormat.format;
        SwapchainCreateInfo.imageColorSpace = VulkanContext::SurfaceFormat.colorSpace;
        SwapchainCreateInfo.imageArrayLayers = 1;
        SwapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        SwapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        SwapchainCreateInfo.presentMode = VulkanContext::PresentMode;
        SwapchainCreateInfo.clipped = VK_TRUE;
        SwapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

        uint32_t QueueFamilyIndices[] = { VulkanContext::Graphics.Index, VulkanContext::Present.Index };
        if (VulkanContext::Graphics.Index != VulkanContext::Present.Index) {
            SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            SwapchainCreateInfo.queueFamilyIndexCount = 2;
            SwapchainCreateInfo.pQueueFamilyIndices = QueueFamilyIndices;
        } else {
            SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            SwapchainCreateInfo.queueFamilyIndexCount = 0;
            SwapchainCreateInfo.pQueueFamilyIndices = nullptr;
        }

        // Finally create the Swapchain
        Window::GetFramebufferSize(Extent.width, Extent.height);
        RecreateSwapchain(VK_NULL_HANDLE);

        PresentInfo = {};
        PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentInfo.swapchainCount = 1;
        PresentInfo.pSwapchains = &Swapchain;
        PresentInfo.waitSemaphoreCount = 1;
        PresentInfo.pImageIndices = &TargetImageViewIndex;

        // Create per frame info
        {
            VkSemaphoreCreateInfo SemaphoreCreateInfo {};
            SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo FenceCreateInfo {};
            FenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            FenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VkCommandPoolCreateInfo CommandPoolCreateInfo {};
            CommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            CommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            CommandPoolCreateInfo.queueFamilyIndex = VulkanContext::Graphics.Index;

            VkCommandBufferAllocateInfo AllocInfo {};
            AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            AllocInfo.commandPool = VK_NULL_HANDLE;
            AllocInfo.commandBufferCount = 1;

            for (FrameData &Frame : Frames) {
                vkCreateSemaphore(VulkanContext::Device, &SemaphoreCreateInfo, nullptr, &Frame.ImageAvailable);
                vkCreateFence(VulkanContext::Device, &FenceCreateInfo, nullptr, &Frame.InFlight);

                vkCreateCommandPool(VulkanContext::Device, &CommandPoolCreateInfo, nullptr, &Frame.CmdPool);
                AllocInfo.commandPool = Frame.CmdPool;
                vkAllocateCommandBuffers(VulkanContext::Device, &AllocInfo, &Frame.CmdBuffer);
            }
        }

        // Fill general rendering information
        Scissor = {
            .offset = { 0, 0 },
            .extent = Extent
        };

        Viewport = {
            .x = 0, .y = 0,
            .width = static_cast<float>(Extent.width),
            .height = static_cast<float>(Extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f
        };

        ColorAttachment = Recipes::ColorAttachment::Terrain();
        // ...
        DepthAttachment = Recipes::DepthAttachment::Default();
        DepthBuffer::Create(Extent.width, Extent.height);
        VkImageView DepthBufferImageView = ImageSystem::View::get(DepthBuffer::ImageView).imageView;
        DepthAttachment.imageView = DepthBufferImageView;

        RenderingInfo = {};
        RenderingInfo.renderArea = {
            .offset = { 0, 0 },
            .extent = Extent
        };
        RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        RenderingInfo.layerCount = 1;
        RenderingInfo.colorAttachmentCount = 1;
        RenderingInfo.pColorAttachments = &ColorAttachment;
        RenderingInfo.pDepthAttachment = &DepthAttachment;

        PipelineCmdBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };

        PipelineCmdSubmitInfo = {};
        PipelineCmdSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        PipelineCmdSubmitInfo.waitSemaphoreCount = 1;
        PipelineCmdSubmitInfo.pWaitDstStageMask = G_PIPELINE_WAIT_STAGES;
        PipelineCmdSubmitInfo.commandBufferCount = 1;
        PipelineCmdSubmitInfo.signalSemaphoreCount = 1;

        if (
            ImGuiRenderer::Create() !=  InferusResult::SUCCESS
        ) {
            spdlog::error("Dear ImGui Renderer creation failed");
            return InferusResult::FAIL;
        }

        if (
            TerrainRenderer::Create(CreationWiseStagingBuffer) !=  InferusResult::SUCCESS
        ) {
            spdlog::error("Terrain Renderer creation failed");
            return InferusResult::FAIL;
        }

        return InferusResult::SUCCESS;
    }

    void Destroy() {
        vkDeviceWaitIdle(VulkanContext::Device);

        TerrainRenderer::Destroy();
        ImGuiRenderer::Destroy();

        BufferSystem::Destroy();
        ImageSystem::Destroy();

        for (FrameData &Frame : Frames) {
            if (Frame.ImageAvailable) { vkDestroySemaphore(VulkanContext::Device, Frame.ImageAvailable, nullptr); }
            if (Frame.InFlight) { vkDestroyFence(VulkanContext::Device, Frame.InFlight, nullptr); }
            if (Frame.CmdPool) { vkDestroyCommandPool(VulkanContext::Device, Frame.CmdPool, nullptr); }
        }

        CleanupSwapchainImages();
        DestroySwapchain(Swapchain);

        VulkanContext::Destroy();
    }

    // TODO: Make this async
    void RecreateSwapchain(VkSwapchainKHR OldSwapchain) {
        vkDeviceWaitIdle(VulkanContext::Device);
        SwapchainCreateInfo.imageExtent = Extent;
        SwapchainCreateInfo.oldSwapchain = OldSwapchain;
        SwapchainCreateInfo.preTransform = SurfaceCapabilities.currentTransform;

        if (vkCreateSwapchainKHR(VulkanContext::Device, &SwapchainCreateInfo, nullptr, &Swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Swapchain creation failed");
        }

        vkGetSwapchainImagesKHR(VulkanContext::Device, Swapchain, &SwapchainImageCount, nullptr);
        std::vector<VkImage> ImagesTemp(SwapchainImageCount);
        vkGetSwapchainImagesKHR(VulkanContext::Device, Swapchain, &SwapchainImageCount, ImagesTemp.data());
        SwapchainImages.resize(SwapchainImageCount);

        CleanupSwapchainImages();
        VkSemaphoreCreateInfo SemaphoreCreateInfo{};
        SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t i = 0; i < SwapchainImageCount; i++) {
            SwapchainImages[i].Image = ImagesTemp[i];
            VkImageViewCreateInfo ImageViewCreateInfo =
                Recipes::ImageViewCreateInfo::Swapchain(SwapchainImages[i].Image, VulkanContext::SurfaceFormat.format);
            if (
                vkCreateImageView(VulkanContext::Device, &ImageViewCreateInfo, nullptr, &SwapchainImages[i].ImageView) != VK_SUCCESS ||
                vkCreateSemaphore(VulkanContext::Device, &SemaphoreCreateInfo, nullptr, &SwapchainImages[i].RenderFinished) != VK_SUCCESS
            ) {
                throw std::runtime_error("Swapchain's Image, ImageView or Semaphore creation failed");
            }
        }
        DestroySwapchain(OldSwapchain);
    }

    void DestroySwapchain(VkSwapchainKHR OldSwapchain) {
        if (Swapchain) { vkDestroySwapchainKHR(VulkanContext::Device, OldSwapchain, nullptr); }
    }

    void CleanupSwapchainImages() {
        for (SwapchainImage& SwpchImage : SwapchainImages) {
            if (SwpchImage.ImageView) { vkDestroyImageView(VulkanContext::Device, SwpchImage.ImageView, nullptr); }
            if (SwpchImage.RenderFinished) { vkDestroySemaphore(VulkanContext::Device, SwpchImage.RenderFinished, nullptr); }
        }
    }

    void QuerySurfaceCapabilities() {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanContext::PhysicalDevice, VulkanContext::Surface, &SurfaceCapabilities);
    }

    void Resize(uint32_t Width, uint32_t Height) {
        if (Width == 0 || Height == 0) return;
        vkDeviceWaitIdle(VulkanContext::Device);
        QuerySurfaceCapabilities();
        VkExtent2D MinExtent = SurfaceCapabilities.minImageExtent;
        VkExtent2D MaxExtent = SurfaceCapabilities.maxImageExtent;
        Extent.width = std::clamp(Width, MinExtent.width, MaxExtent.width);
        Extent.height = std::clamp(Height, MinExtent.height, MaxExtent.height);
        Scissor.extent = Extent;
        Viewport.width = static_cast<float>(Extent.width);
        Viewport.height = static_cast<float>(Extent.height);
        RenderingInfo.renderArea = {
            .offset = { 0, 0 },
            .extent = Extent
        };
        DepthBuffer::Recreate(Width, Height);
        VkImageView DepthBufferImageView = ImageSystem::View::get(DepthBuffer::ImageView).imageView;
        DepthAttachment.imageView = DepthBufferImageView;

        RecreateSwapchain(Swapchain);
    }

    void Render() {
        FrameData& TargetFrame = Frames[TargetFrameIndex];
        VkCommandBuffer& cmd = TargetFrame.CmdBuffer;

        vkWaitForFences(VulkanContext::Device, 1, &TargetFrame.InFlight, VK_TRUE, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(
            VulkanContext::Device,
            Swapchain,
            UINT64_MAX,
            TargetFrame.ImageAvailable,
            VK_NULL_HANDLE,
            &TargetImageViewIndex
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return;
        }

        vkResetCommandBuffer(cmd, 0);
        vkBeginCommandBuffer(cmd, &PipelineCmdBeginInfo);

        vkResetFences(VulkanContext::Device, 1, &TargetFrame.InFlight);

        VkImageMemoryBarrier RenderingBarrier =
            Recipes::ImageMemoryBarrier::Rendering::EnableRendering(SwapchainImages[TargetImageViewIndex].Image);
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        vkCmdPipelineBarrier(
            cmd,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &RenderingBarrier
        );

        ColorAttachment.imageView = SwapchainImages[TargetImageViewIndex].ImageView;
        vkCmdBeginRendering(cmd, &RenderingInfo);
        vkCmdSetViewport(cmd, 0, 1, &Viewport);
        vkCmdSetScissor(cmd, 0, 1, &Scissor);

        // Actual frame begins

        TerrainRenderer::Render(cmd);

        ImGuiRenderer::Render(cmd);

        // Actual frame ends

        vkCmdEndRendering(cmd);

        VkImageMemoryBarrier PresentingBarrier =
            Recipes::ImageMemoryBarrier::Rendering::EnablePresenting(SwapchainImages[TargetImageViewIndex].Image);
        VkPipelineStageFlags srcStage2 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkPipelineStageFlags dstStage2 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            srcStage2,
            dstStage2,
            0,
            0, nullptr,
            0, nullptr,
            1, &PresentingBarrier
        );

        vkEndCommandBuffer(cmd);

        VkSemaphore RenderWaitSemaphores[] = { TargetFrame.ImageAvailable };
        VkSemaphore RenderSignalSemaphores[] = { SwapchainImages[TargetImageViewIndex].RenderFinished };

        PipelineCmdSubmitInfo.pCommandBuffers = &cmd;
        PipelineCmdSubmitInfo.pWaitSemaphores = RenderWaitSemaphores;
        PipelineCmdSubmitInfo.pSignalSemaphores = RenderSignalSemaphores;

        PresentInfo.pWaitSemaphores = RenderSignalSemaphores;

        vkQueueSubmit(VulkanContext::Graphics.Queue, 1, &PipelineCmdSubmitInfo, TargetFrame.InFlight);
        vkQueuePresentKHR(VulkanContext::Present.Queue, &PresentInfo);

        TargetFrameIndex = (TargetFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}
