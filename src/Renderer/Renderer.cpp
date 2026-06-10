#include "Renderer.hpp"

#include <array>
#include <vector>

#include "VkVault.hpp"
#include "Passes/ImGuiPass.hpp"
#include "Engine/Core/Window.hpp"
#include "Renderer/Resources/ResourceManager.hpp"

namespace Renderer {
    // Frame data
    struct FrameData {
        VkCommandPool CmdPool = VK_NULL_HANDLE;
        VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        u64 LastSignaledValue = 0;
    };

    TimelineSemaphore FrameSemaphore;
    std::array<FrameData, RendererConfig::MAX_FRAMES_IN_FLIGHT> Frames;

    u32 TargetFrameIndex = 0;
    u32 TargetImageViewIndex = 0;

    // Other pipeline data
    VkRect2D Scissor {};
    VkViewport Viewport {};
    VkRenderingAttachmentInfo ColorAttachment {};
    VkRenderingAttachmentInfo DepthAttachment {};
    VkRenderingInfo RenderingInfo {};

    VkCommandBufferBeginInfo RenderingCmdBeginInfo {};

    namespace Swapchain {
        struct SwapchainImage {
            VkImage Image = VK_NULL_HANDLE;
            VkImageView ImageView = VK_NULL_HANDLE;
            VkSemaphore RenderFinished = VK_NULL_HANDLE;
        };

        VkExtent2D Extent;
        VkSwapchainKHR Swapchain;

        VkPresentInfoKHR PresentInfo {};

        std::vector<SwapchainImage> Images;

        IncResult Create();
        void Destroy();

        // Implies recreation btw
        IncResult Resize(u32 width, u32 height);
    }

    namespace DepthBuffer {
        Image::Id Image;
        ImageView::Id ImageView;

        void Create(u32 width, u32 height);
        void Destroy();

        void Resize(u32 width, u32 height);
    }

    static constexpr VkPipelineStageFlags GRAPHICS_PIPELINE_WAIT_STAGES[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    IncResult Create() {
        INC_CHECK(VkVault::Create(), "vulkan context creation failed");

        Swapchain::Create();

        // Create per frame info
        VkSemaphoreCreateInfo semaphore_create_info {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkCommandPoolCreateInfo render_cmd_pool_create_info {};
        render_cmd_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        render_cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        render_cmd_pool_create_info.queueFamilyIndex = VkVault::Graphics.Index;

        VkCommandBufferAllocateInfo cmd_buffer_alloc_info {};
        cmd_buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_alloc_info.commandPool = VK_NULL_HANDLE;
        cmd_buffer_alloc_info.commandBufferCount = 1;

        FrameSemaphore = CreateTimelineSemaphore();
        for (FrameData &frame : Frames) {
            vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &frame.ImageAvailable);

            vkCreateCommandPool(VkVault::Device, &render_cmd_pool_create_info, nullptr, &frame.CmdPool);
            cmd_buffer_alloc_info.commandPool = frame.CmdPool;
            vkAllocateCommandBuffers(VkVault::Device, &cmd_buffer_alloc_info, &frame.CmdBuffer);
        }

        // Fill general rendering information
        Scissor = {
            .offset = { 0, 0 },
            .extent = Swapchain::Extent
        };

        Viewport = {
            .x = 0, .y = 0,
            .width = static_cast<float>(Swapchain::Extent.width),
            .height = static_cast<float>(Swapchain::Extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f
        };

        ColorAttachment = {};
        ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ColorAttachment.clearValue.color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } };

        DepthBuffer::Create(Swapchain::Extent.width, Swapchain::Extent.height);
        DepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        DepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        RenderingInfo = {};
        RenderingInfo.renderArea = {
            .offset = { 0, 0 },
            .extent = Swapchain::Extent
        };
        RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        RenderingInfo.layerCount = 1;
        RenderingInfo.colorAttachmentCount = 1;
        RenderingInfo.pColorAttachments = &ColorAttachment;
        RenderingInfo.pDepthAttachment = &DepthAttachment;

        RenderingCmdBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };

        INC_CHECK(ImGuiPass::Create(), "failed to create imgui context");

        return IncResult::SUCCESS;
    }

    void Destroy() {
        vkDeviceWaitIdle(VkVault::Device);

        DestroyTimelineSemaphore(FrameSemaphore);
        for (FrameData &frame : Frames) {
            if (frame.CmdPool) { vkDestroyCommandPool(VkVault::Device, frame.CmdPool, nullptr); }
            if (frame.ImageAvailable) { vkDestroySemaphore(VkVault::Device, frame.ImageAvailable, nullptr); }
        }

        ImGuiPass::Destroy();
        Swapchain::Destroy();
        DepthBuffer::Destroy();
        VkVault::Destroy();
    }

    void Frame() {
        FrameData& target_frame = Frames[TargetFrameIndex];

        // Rendering
        VkCommandBuffer& render_cmd = target_frame.CmdBuffer;
        VkSemaphoreWaitInfo wait_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = nullptr,
            .flags = 0,
            .semaphoreCount = 1,
            .pSemaphores = &FrameSemaphore.Handle,
            .pValues = &target_frame.LastSignaledValue
        };
        vkWaitSemaphores(VkVault::Device, &wait_info, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(
            VkVault::Device,
            Swapchain::Swapchain,
            UINT64_MAX,
            target_frame.ImageAvailable,
            VK_NULL_HANDLE,
            &TargetImageViewIndex
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(VkVault::Device);
            return;
        }

        vkResetCommandBuffer(render_cmd, 0);
        vkBeginCommandBuffer(render_cmd, &RenderingCmdBeginInfo);

        // Has transfers
        bool has_transfers  = false;
        if (has_transfers) {
            // Record the copies (vkCmdCopyBuffer, vkCmdUpdateBuffer, etc.)
            // GraphicsTransfer(render_cmd);

            VkMemoryBarrier transfer_sync_barrier = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INDEX_READ_BIT
            };

            // Command the GPU to pause the graphics pipeline until transfers finish
            vkCmdPipelineBarrier(
                render_cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                0,
                1, &transfer_sync_barrier,
                0, nullptr, 0, nullptr
            );
        }

        VkImageMemoryBarrier rendering_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Swapchain::Images[TargetImageViewIndex].Image,
            .subresourceRange {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        vkCmdPipelineBarrier(
            render_cmd,
            src_stage, dst_stage,
            0, 0, nullptr, 0, nullptr, 1,
            &rendering_barrier
        );

        ColorAttachment.imageView = Swapchain::Images[TargetImageViewIndex].ImageView;
        vkCmdBeginRendering(render_cmd, &RenderingInfo);
        vkCmdSetViewport(render_cmd, 0, 1, &Viewport);
        vkCmdSetScissor(render_cmd, 0, 1, &Scissor);

        // Actual frame begins

        ImGuiPass::Render(render_cmd);

        // Actual frame ends

        vkCmdEndRendering(render_cmd);

        VkImageMemoryBarrier presenting_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Swapchain::Images[TargetImageViewIndex].Image,
            .subresourceRange {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        VkPipelineStageFlags src_stage_2 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkPipelineStageFlags dst_stage_2 = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        vkCmdPipelineBarrier(
            render_cmd,
            src_stage_2, dst_stage_2,
            0, 0, nullptr, 0, nullptr, 1,
            &presenting_barrier
        );

        vkEndCommandBuffer(render_cmd);

        VkSemaphore submit_wait_semaphores[] = { target_frame.ImageAvailable };
        VkSemaphore submit_signal_semaphores[] = {
            Swapchain::Images[TargetImageViewIndex].RenderFinished, // Signals Present
            FrameSemaphore.Handle                                   // Signals the Timeline
        };

        // Map the timeline values (1-to-1 with the signal array above)
        u64 signal_value = ++FrameSemaphore.LastSignaledValue;
        u64 signal_values[] = {
            0,             // Ignored by the driver for the binary RenderFinished semaphore
            signal_value   // Applied to the timeline Graphics semaphore
        };

        VkTimelineSemaphoreSubmitInfo timeline_semaphore_submit_info = {
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreValueCount = 0, // We're only waiting on a binary semaphore, so 0 is fine
            .pWaitSemaphoreValues = nullptr,
            .signalSemaphoreValueCount = 2,
            .pSignalSemaphoreValues = signal_values
        };

        // Assemble the Submit Info
        VkSubmitInfo render_cmd_submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timeline_semaphore_submit_info,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = submit_wait_semaphores,
            .pWaitDstStageMask = GRAPHICS_PIPELINE_WAIT_STAGES,
            .commandBufferCount = 1,
            .pCommandBuffers = &render_cmd,
            .signalSemaphoreCount = 2,
            .pSignalSemaphores = submit_signal_semaphores
        };

        u32 frame_submission_count = 1;
        VkSubmitInfo utils_cmd_submit_info = {};
        VkSubmitInfo frame_submit[] = { render_cmd_submit_info, utils_cmd_submit_info };

        // Utility commands, image format transfers and acquire/release non frame dependant operations
        bool has_utils = false;
        if (has_utils) {
            frame_submission_count = 2;
            // Fetch this later
            VkCommandBuffer utils_cmd = VK_NULL_HANDLE;
            VkSemaphore external_signal_semaphore = VK_NULL_HANDLE;

            utils_cmd_submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr, // Add timeline info here
                .waitSemaphoreCount = 0,
                .pWaitSemaphores = nullptr,
                .pWaitDstStageMask = nullptr,
                .commandBufferCount = 1,
                .pCommandBuffers = &utils_cmd,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &external_signal_semaphore
            };
        }

        // Submit
        vkQueueSubmit(
            VkVault::Graphics.Queue,
            frame_submission_count,
            frame_submit,
            VK_NULL_HANDLE
        );

        Swapchain::PresentInfo.pWaitSemaphores = &Swapchain::Images[TargetImageViewIndex].RenderFinished;
        vkQueuePresentKHR(VkVault::Present.Queue, &Swapchain::PresentInfo);

        // Save the timeline value so the CPU can wait on it next time!
        target_frame.LastSignaledValue = signal_value;

        TargetFrameIndex = (TargetFrameIndex + 1) % RendererConfig::MAX_FRAMES_IN_FLIGHT;
    }

    void Resize(i32 width, i32 height) {
        if (width == 0 || height == 0) {
            return;
        }
        u32 uw = static_cast<u32>(width);
        u32 uh = static_cast<u32>(height);
        vkDeviceWaitIdle(VkVault::Device);
        Swapchain::Resize(uw, uh);
        DepthBuffer::Resize(uw, uh);
    }

    void BindCamera(Camera3D* camera) {
        CurrentCamera = camera;
    }

    namespace Swapchain {
        VkSwapchainCreateInfoKHR CreateInfo {};

        // Calling with old_swapchain = VK_NULL_HANDLE is the equivalent as creating a new one
        IncResult Recreate(VkSwapchainKHR old_swapchain);
        void Destroy(VkSwapchainKHR old_swapchain);
        void CleanupImages();

        IncResult Create() {
            auto capabilities = VkVault::QuerySurfaceCapabilities();
            Extent = capabilities.currentExtent;
            ImageCount = capabilities.minImageCount + 1;

            CreateInfo = {};
            CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            CreateInfo.surface = VkVault::Surface;
            CreateInfo.minImageCount = Swapchain::ImageCount;
            CreateInfo.imageFormat = VkVault::SurfaceFormat.format;
            CreateInfo.imageColorSpace = VkVault::SurfaceFormat.colorSpace;
            CreateInfo.imageArrayLayers = 1;
            CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            CreateInfo.presentMode = VkVault::PresentMode;
            CreateInfo.clipped = VK_TRUE;
            CreateInfo.oldSwapchain = VK_NULL_HANDLE;

            u32 QueueFamilyIndices[] = { VkVault::Graphics.Index, VkVault::Present.Index };
            if (VkVault::Graphics.Index != VkVault::Present.Index) {
                CreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                CreateInfo.queueFamilyIndexCount = 2;
                CreateInfo.pQueueFamilyIndices = QueueFamilyIndices;
            } else {
                CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                CreateInfo.queueFamilyIndexCount = 0;
                CreateInfo.pQueueFamilyIndices = nullptr;
            }

            // Finally create the Swapchain
            i32 w, h;
            Window::GetFramebufferSize(w, h);
            Extent.width = static_cast<u32>(w);
            Extent.height = static_cast<u32>(h);

            INC_CHECK(Recreate(VK_NULL_HANDLE), "failed to create the swapchain on startup");

            PresentInfo = {};
            PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            PresentInfo.swapchainCount = 1;
            PresentInfo.pSwapchains = &Swapchain;
            PresentInfo.waitSemaphoreCount = 1;
            PresentInfo.pImageIndices = &TargetImageViewIndex;

            return IncResult::SUCCESS;
        }

        void Destroy() {
            CleanupImages();
            Destroy(Swapchain);
        }

        IncResult Resize(u32 width, u32 height) {
            auto capabilities = VkVault::QuerySurfaceCapabilities();
            VkExtent2D min_extent = capabilities.minImageExtent;
            VkExtent2D max_extent = capabilities.maxImageExtent;
            auto clamp = [](auto val, auto min, auto max) { return (val < min) ? min : (val > max) ? max : val; };
            Extent.width = clamp(width, min_extent.width, max_extent.width);
            Extent.height = clamp(height, min_extent.height, max_extent.height);
            Scissor.extent = Extent;
            Viewport.width = static_cast<float>(Extent.width);
            Viewport.height = static_cast<float>(Extent.height);
            RenderingInfo.renderArea = {
                .offset = { 0, 0 },
                .extent = Extent
            };

            INC_CHECK(Recreate(Swapchain), "failed to recreate the swapchain on a resize event w:{} - h:{}", width, height);

            return IncResult::SUCCESS;
        }

        IncResult Recreate(VkSwapchainKHR old_swapchain) {
            vkDeviceWaitIdle(VkVault::Device);
            CreateInfo.imageExtent = Extent;
            CreateInfo.oldSwapchain = old_swapchain;
            auto capabilities = VkVault::QuerySurfaceCapabilities();
            CreateInfo.preTransform = capabilities.currentTransform;

            VK_CHECK(vkCreateSwapchainKHR(VkVault::Device, &CreateInfo, nullptr, &Swapchain), "swapchain creation failed");

            vkGetSwapchainImagesKHR(VkVault::Device, Swapchain, &ImageCount, nullptr);
            std::vector<VkImage> ImagesTemp(ImageCount);
            vkGetSwapchainImagesKHR(VkVault::Device, Swapchain, &ImageCount, ImagesTemp.data());
            Images.resize(ImageCount);

            CleanupImages();
            VkSemaphoreCreateInfo semaphore_create_info {};
            semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkImageViewCreateInfo swapchain_image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = VK_NULL_HANDLE, // to fill later
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VkVault::SurfaceFormat.format,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            for (u32 i = 0; i < ImageCount; i++) {
                Images[i].Image = ImagesTemp[i];
                swapchain_image_view_create_info.image = Images[i].Image;
                VK_CHECK(
                    vkCreateImageView(VkVault::Device, &swapchain_image_view_create_info, nullptr, &Images[i].ImageView),
                    "swapchain image view creation failed"
                );
                VK_CHECK(
                    vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &Images[i].RenderFinished),
                    "swapchain semaphore creation failed"
                );
            }

            Destroy(old_swapchain);

            return IncResult::SUCCESS;
        }

        void Destroy(VkSwapchainKHR old_swapchain) {
            if (Swapchain) { vkDestroySwapchainKHR(VkVault::Device, old_swapchain, nullptr); }
        }

        void CleanupImages() {
            for (SwapchainImage& image : Images) {
                if (image.ImageView) { vkDestroyImageView(VkVault::Device, image.ImageView, nullptr); }
                if (image.RenderFinished) { vkDestroySemaphore(VkVault::Device, image.RenderFinished, nullptr); }
            }
        }
    }

    namespace DepthBuffer {
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

        void Create(u32 width, u32 height) {
            Image::CreateInfo image_create_info {};
            image_create_info.Width = width;
            image_create_info.Height = height;
            image_create_info.Format = RendererConfig::DepthBuffer::Format;
            image_create_info.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            Image = Image::Add(image_create_info);
            Image::Value* depth_image_value = Image::Get(DepthBuffer::Image);
            depth_image_value->Format = RendererConfig::DepthBuffer::Format;

            // Despite having a creation format the image still starts as a _UNDEFINED, so transit it a first time
            VkCommandBuffer cmd = VkVault::SingleTimeCmdBegin(VkVault::Graphics);

            VkImageMemoryBarrier barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = depth_image_value->Image,
                .subresourceRange = Range
            };
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

            vkCmdPipelineBarrier(
                cmd,
                srcStage, dstStage,
                0, 0, nullptr, 0, nullptr, 1,
                &barrier
            );

            VkVault::SingleTimeCmdSubmit(VkVault::Graphics, cmd);

            VkImageViewCreateInfo image_view_create_info = ImageView::FillCreateInfo(depth_image_value);
            image_view_create_info.subresourceRange = DepthBuffer::Range;

            ImageView = ImageView::Add(image_view_create_info);
            DepthAttachment.imageView = ImageView::Get(DepthBuffer::ImageView)->ImageView;
        }

        void Destroy() {
            Image::Del(Image);
            ImageView::Del(ImageView);
        }

        void Resize(u32 width, u32 height) {
            Image::Del(Image);
            ImageView::Del(ImageView);
            Create(width, height);
        }
    }

}
