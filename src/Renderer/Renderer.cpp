#include "Renderer.hpp"

#include <array>
#include <vector>

#include <glm/mat4x4.hpp>

#include "VkVault.hpp"
#include "Passes/ImGuiPass.hpp"
#include "Passes/TerrainPass.hpp"
#include "Engine/Core/Window.hpp"
#include "Renderer/Resources/TransferPipe.hpp"
#include "Renderer/Resources/ResourceManager.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

namespace Renderer {
    // Per frame data used to track the frame submission structure
    struct FrameData {
        VkCommandPool CmdPool = VK_NULL_HANDLE;
        VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        u64 LastSignaledValue = 0;
    };

    TimelineSemaphore FrameSemaphore;
    std::array<FrameData, RendererConfig::MAX_FRAMES_IN_FLIGHT> Frames;

    // Camera UBO Descriptor
    namespace GlobalDescriptors {
        VkPipelineLayout BaseLayout = VK_NULL_HANDLE;

        struct CameraUBO {
            glm::mat4 mvp;
        };
        std::array<Buffer::Id, RendererConfig::MAX_FRAMES_IN_FLIGHT> CameraUBOBuffer;

        std::array<VkDescriptorSet, RendererConfig::MAX_FRAMES_IN_FLIGHT> Sets = { VK_NULL_HANDLE };

        void Create();
        void Destroy();
    }

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
        INC_CHECK(ResourceManager::Initialize(), "resource manager creation failed");
        INC_CHECK(TransferPipe::Create(), "transfer pipe creation failed");
        INC_CHECK(DescriptorManager::Create(), "descriptor manager creation failed");

        Swapchain::Create();

        // Create per frame info
        VkSemaphoreCreateInfo semaphore_create_info {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkCommandPoolCreateInfo render_cmd_poll_create_info {};
        render_cmd_poll_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        render_cmd_poll_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        render_cmd_poll_create_info.queueFamilyIndex = VkVault::Graphics.Index;

        VkCommandBufferAllocateInfo cmd_buffer_alloc_info {};
        cmd_buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_alloc_info.commandPool = VK_NULL_HANDLE;
        cmd_buffer_alloc_info.commandBufferCount = 1;

        FrameSemaphore = CreateTimelineSemaphore();
        for (FrameData &frame : Frames) {
            vkCreateSemaphore(VkVault::Device, &semaphore_create_info, nullptr, &frame.ImageAvailable);

            vkCreateCommandPool(VkVault::Device, &render_cmd_poll_create_info, nullptr, &frame.CmdPool);
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

        // Other essential rendering things
        GlobalDescriptors::Create();

        // Render passes
        INC_CHECK(ImGuiPass::Create(), "failed to create imgui context");
        INC_CHECK(TerrainPass::Create(), "failed to create terrain pass");

        return IncResult::SUCCESS;
    }

    void Destroy() {
        vkDeviceWaitIdle(VkVault::Device);

        DestroyTimelineSemaphore(FrameSemaphore);
        for (FrameData &frame : Frames) {
            if (frame.CmdPool) { vkDestroyCommandPool(VkVault::Device, frame.CmdPool, nullptr); }
            if (frame.ImageAvailable) { vkDestroySemaphore(VkVault::Device, frame.ImageAvailable, nullptr); }
        }

        TerrainPass::Destroy();
        ImGuiPass::Destroy();
        GlobalDescriptors::Destroy();
        Swapchain::Destroy();
        DepthBuffer::Destroy();
        DescriptorManager::Destroy();
        TransferPipe::Destroy();
        ResourceManager::Shutdown();
        VkVault::Destroy();
    }

    void Frame() {
        // Update context
        FrameData& target_frame = Frames[FrameContext.FrameInFlightIndex];
        FrameContext.DrawCommand = target_frame.CmdBuffer;

        WaitOnTimelineSemaphore(FrameSemaphore, target_frame.LastSignaledValue);

        VkResult result = vkAcquireNextImageKHR(
            VkVault::Device,
            Swapchain::Swapchain,
            UINT64_MAX,
            target_frame.ImageAvailable,
            VK_NULL_HANDLE,
            &FrameContext.ImageViewIndex
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(VkVault::Device);
            return;
        }

        vkResetCommandBuffer(FrameContext.DrawCommand, 0);
        vkBeginCommandBuffer(FrameContext.DrawCommand, &RenderingCmdBeginInfo);

        // Frame sensible transfers, will be completed before the begin of the drawing phase
        {
            // Camera UBO for descriptor
            {
                auto ubo_buffer = Buffer::Get(GlobalDescriptors::CameraUBOBuffer[FrameContext.FrameInFlightIndex]);
                GlobalDescriptors::CameraUBO ubo_data = { CurrentCamera->ModelViewProjection };

                vkCmdUpdateBuffer(
                    FrameContext.DrawCommand,
                    ubo_buffer->Buffer,
                    0,
                    sizeof(GlobalDescriptors::CameraUBO),
                    &ubo_data
                );
            }

            // Barriers to hold the drawing back
            VkMemoryBarrier transfer_sync_barrier = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INDEX_READ_BIT
            };

            vkCmdPipelineBarrier(
                FrameContext.DrawCommand,
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
            .image = Swapchain::Images[FrameContext.ImageViewIndex].Image,
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
            FrameContext.DrawCommand,
            src_stage, dst_stage,
            0, 0, nullptr, 0, nullptr, 1,
            &rendering_barrier
        );

        ColorAttachment.imageView = Swapchain::Images[FrameContext.ImageViewIndex].ImageView;
        vkCmdBeginRendering(FrameContext.DrawCommand, &RenderingInfo);
        vkCmdSetViewport(FrameContext.DrawCommand, 0, 1, &Viewport);
        vkCmdSetScissor(FrameContext.DrawCommand, 0, 1, &Scissor);

        // Bind SET 0 for the entire frame
        vkCmdBindDescriptorSets(
            FrameContext.DrawCommand,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            GlobalDescriptors::BaseLayout,
            0, // firstSet = 0
            1, // descriptorSetCount = 1
            &GlobalDescriptors::Sets[FrameContext.FrameInFlightIndex],
            0,
            nullptr
        );

        // Actual frame begins

        ImGuiPass::Render();

        //TerrainPass::Render();

        // Actual frame ends

        vkCmdEndRendering(FrameContext.DrawCommand);

        VkImageMemoryBarrier presenting_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Swapchain::Images[FrameContext.ImageViewIndex].Image,
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
            FrameContext.DrawCommand,
            src_stage_2, dst_stage_2,
            0, 0, nullptr, 0, nullptr, 1,
            &presenting_barrier
        );

        vkEndCommandBuffer(FrameContext.DrawCommand);

        TimelineSemaphoreValue* frame_semaphore_value = GetTimelineSemaphoreValue(FrameSemaphore);

        VkSemaphore submit_wait_semaphores[] = { target_frame.ImageAvailable };
        VkSemaphore submit_signal_semaphores[] = {
            Swapchain::Images[FrameContext.ImageViewIndex].RenderFinished, // Signals Present
            frame_semaphore_value->Semaphore                                   // Signals the Timeline
        };

        // Map the timeline values (1-to-1 with the signal array above)
        u64 signal_value = ++frame_semaphore_value->LastSignaledValue;
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
            .pCommandBuffers = &FrameContext.DrawCommand,
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

        Swapchain::PresentInfo.pWaitSemaphores = &Swapchain::Images[FrameContext.ImageViewIndex].RenderFinished;
        vkQueuePresentKHR(VkVault::Present.Queue, &Swapchain::PresentInfo);

        // Save the timeline value so the CPU can wait on it next time!
        target_frame.LastSignaledValue = signal_value;

        FrameContext.FrameInFlightIndex =
            (FrameContext.FrameInFlightIndex + 1) % RendererConfig::MAX_FRAMES_IN_FLIGHT;
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

    namespace GlobalDescriptors {
        void Create() {
            Buffer::CreateInfo create_info = {
                .Size = sizeof(CameraUBO),
                .Type = Buffer::Type::UBO
            };
            for (Buffer::Id& id : CameraUBOBuffer) {
                id = Buffer::Add(create_info);
            }

            DescriptorManager::AllocateSets(
                DescriptorManager::GlobalLayout,
                RendererConfig::MAX_FRAMES_IN_FLIGHT,
                GlobalDescriptors::Sets.data()
            );

            // Loop through each frame in flight and write both bindings
            for (u32 i = 0; i < RendererConfig::MAX_FRAMES_IN_FLIGHT; ++i) {

                // Write the camera ubo buffer
                auto camera_ubo_buffer_value = Buffer::Get(CameraUBOBuffer[i]);
                VkDescriptorBufferInfo camera_ubo_descriptor_info {};
                camera_ubo_descriptor_info.buffer = camera_ubo_buffer_value->Buffer;
                camera_ubo_descriptor_info.offset = 0;
                camera_ubo_descriptor_info.range = camera_ubo_buffer_value->Size;

                VkWriteDescriptorSet camera_ubo_write {};
                camera_ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                camera_ubo_write.dstSet = GlobalDescriptors::Sets[i];
                camera_ubo_write.dstBinding = DescriptorMap::Global::Binding_CameraUBO;
                camera_ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                camera_ubo_write.descriptorCount = 1;
                camera_ubo_write.pBufferInfo = &camera_ubo_descriptor_info;

                // Execute writes for Set[i]
                vkUpdateDescriptorSets(VkVault::Device, 1, &camera_ubo_write, 0, nullptr);
            }

            VkPipelineLayoutCreateInfo base_layout_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &DescriptorManager::GlobalLayout,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = nullptr
            };

            VK_OUT(
                vkCreatePipelineLayout(VkVault::Device, &base_layout_info, nullptr, &BaseLayout),
                "global base pipeline layout creation failed"
            );
        }

        void Destroy() {
            for (Buffer::Id& id : CameraUBOBuffer) {
                Buffer::Del(id);
            }
            if (BaseLayout) { vkDestroyPipelineLayout(VkVault::Device, BaseLayout, nullptr); }
        }
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
            PresentInfo.pImageIndices = &FrameContext.ImageViewIndex;

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
