#include "Renderer.hpp"

#include <array>
#include <vector>

#include "VkVault.hpp"
#include "Camera.hpp"
#include "Passes/Pass.hpp"
#include "Engine/Core/Window.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

#include "Renderer/Vk/LeanVk.hpp"

IncResult Renderer::Init() {
    Passes = { &TerrainPass, &ImGuiPass };

    INC_CHECK(VkVault::Create(), "vulkan context creation failed");
    INC_CHECK(TransferPipe.Init(), "transfer pipe creation failed");
    INC_CHECK(DescriptorManager::Create(), "descriptor manager creation failed");

    INC_CHECK(InitSwapchain(), "swapchain creation failed");

    SubmissionPile.Reset();

    INC_CHECK(FrameSemaphore.Init(), "frame semaphore creation failed");

    for (FrameData &frame : Frames) {
        INC_CHECK(frame.ImageAvailable.Init(), "frame image-available semaphore creation failed");

        INC_CHECK(frame.FrameBlock.Init(QueueRole::Graphics), "frame command buffer block creation failed");
    }

    // Fill general rendering information
    Scissor = {
        .offset = { 0, 0 },
        .extent = SwapchainExtent
    };

    Viewport = {
        .x = 0, .y = 0,
        .width = static_cast<float>(SwapchainExtent.width),
        .height = static_cast<float>(SwapchainExtent.height),
        .minDepth = 0.0f, .maxDepth = 1.0f
    };

    ColorAttachment = {};
    ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAttachment.clearValue.color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } };

    INC_CHECK(InitDepthBuffer(SwapchainExtent.width, SwapchainExtent.height), "depth buffer creation failed");
    DepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    DepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    RenderingInfo = {};
    RenderingInfo.renderArea = {
        .offset = { 0, 0 },
        .extent = SwapchainExtent
    };
    RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    RenderingInfo.layerCount = 1;
    RenderingInfo.colorAttachmentCount = 1;
    RenderingInfo.pColorAttachments = &ColorAttachment;
    RenderingInfo.pDepthAttachment = &DepthAttachment;

    // Other essential rendering things
    INC_CHECK(InitGlobalDescriptors(), "global descriptors creation failed");

    // Render passes
    for (Pass* pass : Passes) {
        INC_CHECK(pass->Init(), "pass initialization failed");
    }

    return IncResult::SUCCESS;
}

void Renderer::Destroy() {
    vkDeviceWaitIdle(VkVault::Device);

    FrameSemaphore.Destroy();
    for (FrameData &frame : Frames) {
        frame.FrameBlock.Destroy();
        frame.ImageAvailable.Destroy();
    }

    for (auto it = Passes.rbegin(); it != Passes.rend(); ++it) { (*it)->Destroy(); }
    DestroyGlobalDescriptors();
    DestroySwapchain();
    DestroyDepthBuffer();
    DescriptorManager::Destroy();
    TransferPipe.Destroy();

    Buffers.DestroyAll();
    Images.DestroyAll();
    ImageViews.DestroyAll();

    VkVault::Destroy();
}

void Renderer::Frame() {
    // Update context
    FrameData& target_frame = Frames[FrameContext.FrameInFlightIndex];

    // Wait for this frame-in-flight slot's previous GPU work to actually finish before
    // resetting its command pool - resetting first would reset buffers that might still be pending.
    FrameSemaphore.Wait(target_frame.LastSignaledValue);

    target_frame.FrameBlock.Reset();
    FrameContext.DrawCommand = target_frame.FrameBlock.GetNext();

    VkResult result = vkAcquireNextImageKHR(
        VkVault::Device,
        SwapchainHandle,
        UINT64_MAX,
        target_frame.ImageAvailable.Semaphore,
        VK_NULL_HANDLE,
        &FrameContext.ImageViewIndex
    );
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(VkVault::Device);
        return;
    }

    // No LeanVk::ResetCommand needed here - target_frame.FrameBlock.Reset() above already
    // reset the whole pool (and the pool was never created with RESET_COMMAND_BUFFER_BIT,
    // so resetting this one buffer individually would be invalid anyway).
    LeanVk::BeginCommand(FrameContext.DrawCommand);

    // Frame sensible transfers, will be completed before the begin of the drawing phase
    {
        // Camera UBO for descriptor
        {
            auto ubo_buffer = Buffers.Get(SceneGlobalsBuffer[FrameContext.FrameInFlightIndex]);
            SceneGlobals ubo_data = {
                .ViewProjection = CurrentCamera->Projection * CurrentCamera->View,
                .CameraPosition = CurrentCamera->Position
            };

            vkCmdUpdateBuffer(
                FrameContext.DrawCommand,
                ubo_buffer->Handle,
                0,
                sizeof(SceneGlobals),
                &ubo_data
            );
        }

        // Passes transfers
        for (Pass* pass : Passes) { pass->FrameSensibleTransfers(); }

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
        .image = SwapchainImages[FrameContext.ImageViewIndex].Image,
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

    ColorAttachment.imageView = SwapchainImages[FrameContext.ImageViewIndex].ImageView;
    vkCmdBeginRendering(FrameContext.DrawCommand, &RenderingInfo);
    vkCmdSetViewport(FrameContext.DrawCommand, 0, 1, &Viewport);
    vkCmdSetScissor(FrameContext.DrawCommand, 0, 1, &Scissor);

    // Bind SET 0 for the entire frame
    vkCmdBindDescriptorSets(
        FrameContext.DrawCommand,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        GlobalDescriptorsBaseLayout,
        0, // firstSet = 0
        1, // descriptorSetCount = 1
        &GlobalDescriptors.Sets[FrameContext.FrameInFlightIndex],
        0,
        nullptr
    );

    // Actual frame begins

    for (Pass* pass : Passes) { pass->Render(); }

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
        .image = SwapchainImages[FrameContext.ImageViewIndex].Image,
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

    LeanVk::EndCommand(FrameContext.DrawCommand);

    // Reclaim any command buffer pools whose prior work has actually finished on the GPU
    TransferPipe.TryReclaimCommandBuffers();

    // Fold pending image ownership acquires into this frame's own submission instead of paying for a second vkQueueSubmit2 -
    // done BEFORE the draw's own submission below so its ticket is known in time to wait on it there.
    TransferPipe.AcquirePending(QueueRole::Graphics, SubmissionPile);

    // Submission structure
    SubmissionPile.BeginSubmission();

    SubmissionPile.AddCommand(FrameContext.DrawCommand);

    SubmissionPile.WaitBinarySemaphore(target_frame.ImageAvailable, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // AcquirePending's barrier is a separate submission entry with no implicit ordering
    // relative to this one - without this wait, the draw could sample a heightmap layer
    // that was just re-streamed before its re-acquire has actually executed.
    Ticket last_acquire_ticket;
    if (TransferPipe.GetLastAcquireTicket(QueueRole::Graphics, last_acquire_ticket)) {
        SubmissionPile.WaitForTicket(last_acquire_ticket, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    }

    SubmissionPile.SignalBinarySemaphore(SwapchainImages[FrameContext.ImageViewIndex].RenderFinished);

    // Hm, seems to be a bad data accesing pattern
    u64 signal_value = ++FrameSemaphore.LastPromissedValue;
    SubmissionPile.SignalTimeline(FrameSemaphore, signal_value);

    SubmissionPile.EndSubmission();

    // Submit
    SubmissionPile.Submit();

    SwapchainPresentInfo.pWaitSemaphores = &SwapchainImages[FrameContext.ImageViewIndex].RenderFinished.Semaphore;
    vkQueuePresentKHR(VkVault::Queues[QueueRole::Present].Queue, &SwapchainPresentInfo);

    // Save the timeline value so the CPU can wait on it next time!
    target_frame.LastSignaledValue = signal_value;

    FrameContext.FrameInFlightIndex =
        (FrameContext.FrameInFlightIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::Resize(i32 width, i32 height) {
    if (width == 0 || height == 0) {
        return;
    }
    u32 uw = static_cast<u32>(width);
    u32 uh = static_cast<u32>(height);
    vkDeviceWaitIdle(VkVault::Device);
    ResizeSwapchain(uw, uh);
    ResizeDepthBuffer(uw, uh);
}

void Renderer::BindCamera(Camera* camera) {
    CurrentCamera = camera;
}

IncResult Renderer::InitGlobalDescriptors() {
    Buffer::CreateInfo create_info = {
        .Size = sizeof(SceneGlobals),
        .Type = Buffer::Type::UBO
    };
    for (BufferId& id : SceneGlobalsBuffer) {
        INC_CHECK(Buffers.Add(create_info, id), "scene globals buffer creation failed");
    }

    DescriptorManager::AllocateSets(
        DescriptorManager::GlobalLayout,
        MAX_FRAMES_IN_FLIGHT,
        GlobalDescriptors.Sets.data()
    );

    // Loop through each frame in flight and write both bindings
    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {

        // Write the camera ubo buffer
        auto camera_ubo_buffer_value = Buffers.Get(SceneGlobalsBuffer[i]);
        VkDescriptorBufferInfo camera_ubo_descriptor_info {};
        camera_ubo_descriptor_info.buffer = camera_ubo_buffer_value->Handle;
        camera_ubo_descriptor_info.offset = 0;
        camera_ubo_descriptor_info.range = camera_ubo_buffer_value->Size;

        VkWriteDescriptorSet camera_ubo_write {};
        camera_ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        camera_ubo_write.dstSet = GlobalDescriptors.Sets[i];
        camera_ubo_write.dstBinding = DescriptorMap::Global::Binding_SceneGlobals;
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

    VK_CHECK(
        vkCreatePipelineLayout(VkVault::Device, &base_layout_info, nullptr, &GlobalDescriptorsBaseLayout),
        "global base pipeline layout creation failed"
    );

    return IncResult::SUCCESS;
}

void Renderer::DestroyGlobalDescriptors() {
    for (BufferId& id : SceneGlobalsBuffer) {
        Buffers.Del(id);
    }
    if (GlobalDescriptorsBaseLayout) { vkDestroyPipelineLayout(VkVault::Device, GlobalDescriptorsBaseLayout, nullptr); }
}

IncResult Renderer::InitSwapchain() {
    auto capabilities = VkVault::QuerySurfaceCapabilities();
    SwapchainExtent = capabilities.currentExtent;
    Swapchain.ImageCount = capabilities.minImageCount + 1;

    SwapchainCreateInfo = {};
    SwapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    SwapchainCreateInfo.surface = VkVault::Surface;
    SwapchainCreateInfo.minImageCount = Swapchain.ImageCount;
    SwapchainCreateInfo.imageFormat = VkVault::SurfaceFormat.format;
    SwapchainCreateInfo.imageColorSpace = VkVault::SurfaceFormat.colorSpace;
    SwapchainCreateInfo.imageArrayLayers = 1;
    SwapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    SwapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    SwapchainCreateInfo.presentMode = VkVault::PresentMode;
    SwapchainCreateInfo.clipped = VK_TRUE;
    SwapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    u32 QueueFamilyIndices[] = { VkVault::Queues[QueueRole::Graphics].FamilyIndex, VkVault::Queues[QueueRole::Present].FamilyIndex };
    if (VkVault::Queues[QueueRole::Graphics].FamilyIndex != VkVault::Queues[QueueRole::Present].FamilyIndex) {
        SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        SwapchainCreateInfo.queueFamilyIndexCount = 2;
        SwapchainCreateInfo.pQueueFamilyIndices = QueueFamilyIndices;
    } else {
        SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        SwapchainCreateInfo.queueFamilyIndexCount = 0;
        SwapchainCreateInfo.pQueueFamilyIndices = nullptr;
    }

    // Finally create the Swapchain
    i32 w, h;
    Window::GetFramebufferSize(w, h);
    SwapchainExtent.width = static_cast<u32>(w);
    SwapchainExtent.height = static_cast<u32>(h);

    INC_CHECK(RecreateSwapchain(VK_NULL_HANDLE), "failed to create the swapchain on startup");

    SwapchainPresentInfo = {};
    SwapchainPresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    SwapchainPresentInfo.swapchainCount = 1;
    SwapchainPresentInfo.pSwapchains = &SwapchainHandle;
    SwapchainPresentInfo.waitSemaphoreCount = 1;
    SwapchainPresentInfo.pImageIndices = &FrameContext.ImageViewIndex;

    return IncResult::SUCCESS;
}

void Renderer::DestroySwapchain() {
    CleanupSwapchainImages();
    DestroySwapchainKHR(SwapchainHandle);
}

IncResult Renderer::ResizeSwapchain(u32 width, u32 height) {
    auto capabilities = VkVault::QuerySurfaceCapabilities();
    VkExtent2D min_extent = capabilities.minImageExtent;
    VkExtent2D max_extent = capabilities.maxImageExtent;
    auto clamp = [](auto val, auto min, auto max) { return (val < min) ? min : (val > max) ? max : val; };
    SwapchainExtent.width = clamp(width, min_extent.width, max_extent.width);
    SwapchainExtent.height = clamp(height, min_extent.height, max_extent.height);
    Scissor.extent = SwapchainExtent;
    Viewport.width = static_cast<float>(SwapchainExtent.width);
    Viewport.height = static_cast<float>(SwapchainExtent.height);
    RenderingInfo.renderArea = {
        .offset = { 0, 0 },
        .extent = SwapchainExtent
    };

    INC_CHECK(RecreateSwapchain(SwapchainHandle), "failed to recreate the swapchain on a resize event w:{} - h:{}", width, height);

    return IncResult::SUCCESS;
}

IncResult Renderer::RecreateSwapchain(VkSwapchainKHR old_swapchain) {
    vkDeviceWaitIdle(VkVault::Device);
    SwapchainCreateInfo.imageExtent = SwapchainExtent;
    SwapchainCreateInfo.oldSwapchain = old_swapchain;
    auto capabilities = VkVault::QuerySurfaceCapabilities();
    SwapchainCreateInfo.preTransform = capabilities.currentTransform;

    VK_CHECK(vkCreateSwapchainKHR(VkVault::Device, &SwapchainCreateInfo, nullptr, &SwapchainHandle), "swapchain creation failed");

    vkGetSwapchainImagesKHR(VkVault::Device, SwapchainHandle, &Swapchain.ImageCount, nullptr);
    std::vector<VkImage> images_temp(Swapchain.ImageCount);
    vkGetSwapchainImagesKHR(VkVault::Device, SwapchainHandle, &Swapchain.ImageCount, images_temp.data());
    SwapchainImages.resize(Swapchain.ImageCount);

    CleanupSwapchainImages();

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

    for (u32 i = 0; i < Swapchain.ImageCount; i++) {
        SwapchainImages[i].Image = images_temp[i];
        swapchain_image_view_create_info.image = SwapchainImages[i].Image;
        VK_CHECK(
            vkCreateImageView(VkVault::Device, &swapchain_image_view_create_info, nullptr, &SwapchainImages[i].ImageView),
            "swapchain image view creation failed"
        );
        INC_CHECK(SwapchainImages[i].RenderFinished.Init(), "swapchain render-finished semaphore creation failed");
    }

    DestroySwapchainKHR(old_swapchain);

    return IncResult::SUCCESS;
}

void Renderer::DestroySwapchainKHR(VkSwapchainKHR swapchain) {
    if (SwapchainHandle) { vkDestroySwapchainKHR(VkVault::Device, swapchain, nullptr); }
}

void Renderer::CleanupSwapchainImages() {
    for (SwapchainImage& image : SwapchainImages) {
        if (image.ImageView) { vkDestroyImageView(VkVault::Device, image.ImageView, nullptr); }
        image.RenderFinished.Destroy();
    }
}

IncResult Renderer::InitDepthBuffer(u32 width, u32 height) {
    Image::CreateInfo image_create_info {};
    image_create_info.Width = width;
    image_create_info.Height = height;
    image_create_info.Format = DepthBufferFormat;
    image_create_info.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_create_info.UsageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    INC_CHECK(Images.Add(image_create_info, DepthBufferImage), "depth buffer image creation failed");
    Image* depth_image_value = Images.Get(DepthBufferImage);
    depth_image_value->Format = DepthBufferFormat;

    // Despite having a creation format the image still starts as a _UNDEFINED, so transit it a first time
    CommandBufferBlock setup_block;
    INC_CHECK(setup_block.Init(QueueRole::Graphics), "depth buffer setup command buffer block creation failed");

    VkCommandBuffer cmd = setup_block.GetNext();
    LeanVk::BeginCommand(cmd);

    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = depth_image_value->UsageLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depth_image_value->Handle,
        .subresourceRange = DepthBufferRange
    };
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    vkCmdPipelineBarrier(
        cmd,
        src_stage, dst_stage,
        0, 0, nullptr, 0, nullptr, 1,
        &barrier
    );

    LeanVk::EndCommand(cmd);

    // `::` needed: this class's own `SubmissionPile` member shadows the template type name.
    ::SubmissionPile<QueueRole::Graphics, 1, 1, 0, 0> one_shot_pile;
    one_shot_pile.BeginSubmission();
    one_shot_pile.AddCommand(cmd);
    one_shot_pile.EndSubmission();
    one_shot_pile.Submit();

    vkQueueWaitIdle(VkVault::Queues[QueueRole::Graphics].Queue);
    setup_block.Destroy();

    VkImageViewCreateInfo image_view_create_info = FillImageViewCreateInfo(depth_image_value);
    image_view_create_info.subresourceRange = DepthBufferRange;

    INC_CHECK(ImageViews.Add(image_view_create_info, DepthBufferImageView), "depth buffer image view creation failed");
    DepthAttachment.imageView = ImageViews.Get(DepthBufferImageView)->Handle;

    return IncResult::SUCCESS;
}

void Renderer::DestroyDepthBuffer() {
    Images.Del(DepthBufferImage);
    ImageViews.Del(DepthBufferImageView);
}

void Renderer::ResizeDepthBuffer(u32 width, u32 height) {
    Images.Del(DepthBufferImage);
    ImageViews.Del(DepthBufferImageView);
    if (InitDepthBuffer(width, height) != IncResult::SUCCESS) {
        analog::critical("depth buffer recreation failed on resize");
    }
}
