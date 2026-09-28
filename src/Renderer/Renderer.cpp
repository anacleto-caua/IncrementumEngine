#include "Renderer.hpp"

#include <array>
#include <vector>

#include "VkVault.hpp"
#include "Camera.hpp"
#include "Passes/Pass.hpp"
#include "Tools/DebugPanel.hpp"
#include "Engine/Core/Window.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

#include "Renderer/Vk/LeanVk.hpp"

IncResult Renderer::Init() {
    // Scene: props before Sky, which only fills pixels nothing else wrote depth to.
    // Overlay: PostPass first (it overwrites every swapchain pixel with the tonemapped scene),
    // then Text/ImGui blended on top - screen-space, so they stay crisp and un-tonemapped.
    ScenePasses = { &TerrainPass, &PropPass, &SkyPass };
    OverlayPasses = { &PostPass, &TextPass, &ImGuiPass };
    Passes = ScenePasses;
    Passes.insert(Passes.end(), OverlayPasses.begin(), OverlayPasses.end());

    INC_CHECK(VkVault::Create(), "vulkan context creation failed");
    INC_CHECK(TransferPipe.Init(), "transfer pipe creation failed");
    INC_CHECK(ComputePipe.Init(), "compute pipe creation failed");
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

    // 4x MSAA is guaranteed by the spec for color/depth framebuffers in general, but the depth
    // FORMAT's own multisample support is per-format - check rather than fail obscurely later.
    {
        VkImageFormatProperties depth_props {};
        vkGetPhysicalDeviceImageFormatProperties(
            VkVault::PhysicalDevice, DepthBufferFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &depth_props
        );
        if ((depth_props.sampleCounts & RendererConstants::SceneSampleCount) == 0) {
            analog::critical("depth format does not support the scene's MSAA sample count");
            return IncResult::FAIL;
        }
    }

    INC_CHECK(InitSceneTargets(SwapchainExtent.width, SwapchainExtent.height), "scene targets creation failed");

    // Scene scope: the MSAA color is only ever resolved, never read back, so its own contents
    // are dropped at the end (STORE_OP_DONT_CARE) - the resolve still happens.
    SceneColorAttachment = {};
    SceneColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    SceneColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    SceneColorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    SceneColorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    SceneColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    SceneColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    SceneColorAttachment.clearValue.color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } };

    SceneDepthAttachment = {};
    SceneDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    SceneDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    SceneDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    SceneDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    SceneDepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    // Image views are (re)filled by InitSceneTargets(); only the static shape lives here.
    SceneColorAttachment.imageView = ImageViews.Get(SceneColorMsaaView)->Handle;
    SceneColorAttachment.resolveImageView = ImageViews.Get(SceneColorResolvedView)->Handle;
    SceneDepthAttachment.imageView = ImageViews.Get(DepthBufferImageView)->Handle;

    SceneRenderingInfo = {};
    SceneRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    SceneRenderingInfo.renderArea = { .offset = { 0, 0 }, .extent = SwapchainExtent };
    SceneRenderingInfo.layerCount = 1;
    SceneRenderingInfo.colorAttachmentCount = 1;
    SceneRenderingInfo.pColorAttachments = &SceneColorAttachment;
    SceneRenderingInfo.pDepthAttachment = &SceneDepthAttachment;

    // Overlay scope: PostPass overwrites every pixel, so the swapchain's previous contents are
    // irrelevant (LOAD_OP_DONT_CARE) - no depth attachment at all.
    OverlayColorAttachment = {};
    OverlayColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    OverlayColorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    OverlayColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    OverlayColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    OverlayRenderingInfo = {};
    OverlayRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    OverlayRenderingInfo.renderArea = { .offset = { 0, 0 }, .extent = SwapchainExtent };
    OverlayRenderingInfo.layerCount = 1;
    OverlayRenderingInfo.colorAttachmentCount = 1;
    OverlayRenderingInfo.pColorAttachments = &OverlayColorAttachment;
    OverlayRenderingInfo.pDepthAttachment = nullptr;

    // Other essential rendering things
    INC_CHECK(InitGlobalDescriptors(), "global descriptors creation failed");

    // Render passes
    for (Pass* pass : Passes) {
        INC_CHECK(pass->Init(), "pass initialization failed");
    }

    // Wonky test for ComputePipe - needs DescriptorManager/ComputePipe
    // INC_CHECK(ComputeTestDemo::Init(), "compute test demo initialization failed");

    return IncResult::SUCCESS;
}

void Renderer::Destroy() {
    vkDeviceWaitIdle(VkVault::Device);

    FrameSemaphore.Destroy();
    for (FrameData &frame : Frames) {
        frame.FrameBlock.Destroy();
        frame.ImageAvailable.Destroy();
    }

    //ComputeTestDemo::Destroy();

    for (auto it = Passes.rbegin(); it != Passes.rend(); ++it) { (*it)->Destroy(); }
    DestroyGlobalDescriptors();
    DestroySwapchain();
    DestroySceneTargets();
    DescriptorManager::Destroy();
    TransferPipe.Destroy();
    ComputePipe.Destroy();

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
            mat4 view_projection = CurrentCamera->Projection * CurrentCamera->View;
            SceneGlobals ubo_data = {
                .ViewProjection = view_projection,
                .CameraPosition = CurrentCamera->Position,
                .InverseViewProjection = math::inverse(view_projection),
                .SunDirection = SunDirection
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

    // ---- Scene scope: HDR + 4x MSAA, resolved into SceneColorResolvedImage on end ----
    {
        // Every scene target is fully cleared (or resolve-overwritten) this frame, so each goes
        // from UNDEFINED (contents discarded). The source scope covers the previous frame's use
        // of these same single-instance images - earlier in this queue's submission order:
        // MSAA color/depth writes (WAW) and PostPass's read of the resolved image (WAR).
        auto color_range = VkImageSubresourceRange {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1
        };
        std::array<VkImageMemoryBarrier, 3> scene_barriers = {{
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Images.Get(SceneColorMsaaImage)->Handle,
                .subresourceRange = color_range
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0, // last use was a read - execution dependency alone covers WAR
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Images.Get(SceneColorResolvedImage)->Handle,
                .subresourceRange = color_range
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Images.Get(DepthBufferImage)->Handle,
                .subresourceRange = DepthBufferRange
            }
        }};
        vkCmdPipelineBarrier(
            FrameContext.DrawCommand,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<u32>(scene_barriers.size()), scene_barriers.data()
        );
    }

    vkCmdBeginRendering(FrameContext.DrawCommand, &SceneRenderingInfo);
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

    DebugPanel::DrawToolbar();
    for (Pass* pass : ScenePasses) { pass->Render(); }

    vkCmdEndRendering(FrameContext.DrawCommand);

    // ---- Overlay scope: tonemap onto the swapchain, then screen-space UI ----
    {
        std::array<VkImageMemoryBarrier, 2> overlay_barriers = {{
            {
                // Resolve writes happen in the color-attachment-output stage.
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = Images.Get(SceneColorResolvedImage)->UsageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Images.Get(SceneColorResolvedImage)->Handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0, .levelCount = 1,
                    .baseArrayLayer = 0, .layerCount = 1
                }
            },
            {
                // Swapchain image: srcStage must include COLOR_ATTACHMENT_OUTPUT so this chains
                // with the acquire semaphore's wait stage in the submission below.
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = SwapchainImages[FrameContext.ImageViewIndex].Image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0, .levelCount = 1,
                    .baseArrayLayer = 0, .layerCount = 1
                }
            }
        }};
        vkCmdPipelineBarrier(
            FrameContext.DrawCommand,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<u32>(overlay_barriers.size()), overlay_barriers.data()
        );
    }

    OverlayColorAttachment.imageView = SwapchainImages[FrameContext.ImageViewIndex].ImageView;
    vkCmdBeginRendering(FrameContext.DrawCommand, &OverlayRenderingInfo);
    vkCmdSetViewport(FrameContext.DrawCommand, 0, 1, &Viewport);
    vkCmdSetScissor(FrameContext.DrawCommand, 0, 1, &Scissor);

    for (Pass* pass : OverlayPasses) { pass->Render(); }

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
    ComputePipe.TryReclaimCommandBuffers();

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

    // Both signals need an explicit ALL_COMMANDS stage mask: SubmissionPile's default is
    // STAGE_2_NONE, which puts none of this submission's work in the signal's first
    // synchronization scope - i.e. present (and the CPU's frame-slot reuse wait below) were
    // formally ordered after nothing. Synchronization validation flagged it as
    // SYNC-HAZARD-PRESENT-AFTER-WRITE on the swapchain images.
    SubmissionPile.SignalBinarySemaphore(SwapchainImages[FrameContext.ImageViewIndex].RenderFinished, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

    // Hm, seems to be a bad data accesing pattern
    u64 signal_value = ++FrameSemaphore.LastPromissedValue;
    SubmissionPile.SignalTimeline(FrameSemaphore, signal_value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

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

    DestroySceneTargets();
    if (InitSceneTargets(SwapchainExtent.width, SwapchainExtent.height) != IncResult::SUCCESS) {
        analog::critical("scene targets recreation failed on resize");
        return;
    }
    SceneColorAttachment.imageView = ImageViews.Get(SceneColorMsaaView)->Handle;
    SceneColorAttachment.resolveImageView = ImageViews.Get(SceneColorResolvedView)->Handle;
    SceneDepthAttachment.imageView = ImageViews.Get(DepthBufferImageView)->Handle;
    PostPass.RebindSceneColor();
}

VkImageView Renderer::GetResolvedSceneColorView() {
    return ImageViews.Get(SceneColorResolvedView)->Handle;
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
    Swapchain.Width = SwapchainExtent.width;
    Swapchain.Height = SwapchainExtent.height;

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
    Swapchain.Width = SwapchainExtent.width;
    Swapchain.Height = SwapchainExtent.height;
    Scissor.extent = SwapchainExtent;
    Viewport.width = static_cast<float>(SwapchainExtent.width);
    Viewport.height = static_cast<float>(SwapchainExtent.height);
    SceneRenderingInfo.renderArea = { .offset = { 0, 0 }, .extent = SwapchainExtent };
    OverlayRenderingInfo.renderArea = { .offset = { 0, 0 }, .extent = SwapchainExtent };

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

IncResult Renderer::InitSceneTargets(u32 width, u32 height) {
    // MSAA color: rendered into and resolved, never sampled - TRANSIENT lets tile-based GPUs
    // keep it on-chip entirely.
    Image::CreateInfo msaa_color_info {};
    msaa_color_info.Width = width;
    msaa_color_info.Height = height;
    msaa_color_info.Format = RendererConstants::SceneColorFormat;
    msaa_color_info.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    msaa_color_info.Samples = RendererConstants::SceneSampleCount;
    msaa_color_info.OwnerQueue = QueueRole::Graphics;
    msaa_color_info.UsageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    INC_CHECK(Images.Add(msaa_color_info, SceneColorMsaaImage), "scene MSAA color image creation failed");
    // Plain 2D views (FillImageViewCreateInfo defaults to 2D_ARRAY) - PostPass samples the
    // resolved one through a sampler2D, which requires a matching 2D view type.
    VkImageViewCreateInfo msaa_view_info = FillImageViewCreateInfo(Images.Get(SceneColorMsaaImage));
    msaa_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    INC_CHECK(ImageViews.Add(msaa_view_info, SceneColorMsaaView), "scene MSAA color view creation failed");

    // Resolved color: its resting layout is the one PostPass samples it in.
    Image::CreateInfo resolved_color_info = msaa_color_info;
    resolved_color_info.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    resolved_color_info.Samples = VK_SAMPLE_COUNT_1_BIT;
    resolved_color_info.UsageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    INC_CHECK(Images.Add(resolved_color_info, SceneColorResolvedImage), "scene resolved color image creation failed");
    VkImageViewCreateInfo resolved_view_info = FillImageViewCreateInfo(Images.Get(SceneColorResolvedImage));
    resolved_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    INC_CHECK(ImageViews.Add(resolved_view_info, SceneColorResolvedView), "scene resolved color view creation failed");

    Image::CreateInfo depth_info {};
    depth_info.Width = width;
    depth_info.Height = height;
    depth_info.Format = DepthBufferFormat;
    depth_info.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_info.Samples = RendererConstants::SceneSampleCount;
    depth_info.OwnerQueue = QueueRole::Graphics;
    depth_info.UsageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    INC_CHECK(Images.Add(depth_info, DepthBufferImage), "depth buffer image creation failed");

    VkImageViewCreateInfo depth_view_info = FillImageViewCreateInfo(Images.Get(DepthBufferImage));
    depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_view_info.subresourceRange = DepthBufferRange;
    INC_CHECK(ImageViews.Add(depth_view_info, DepthBufferImageView), "depth buffer image view creation failed");

    return IncResult::SUCCESS;
}

void Renderer::DestroySceneTargets() {
    ImageViews.Del(SceneColorMsaaView);
    Images.Del(SceneColorMsaaImage);
    ImageViews.Del(SceneColorResolvedView);
    Images.Del(SceneColorResolvedImage);
    ImageViews.Del(DepthBufferImageView);
    Images.Del(DepthBufferImage);
}
