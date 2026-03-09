#pragma once

#include <array>
#include <cstdint>

#include <glm/fwd.hpp>
#include <glm/ext.hpp>
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "Engine/Types.hpp"
#include "Engine/InferusRenderer/Passes/ImGuiRenderer.hpp"
#include "Engine/InferusRenderer/Passes/TerrainRenderer.hpp"

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

class InferusRenderer {
public:
    static constexpr size_t CREATION_WISE_STAGING_BUFFER_SIZE = 1 * 1024 * 1024;
    // Swapchain
    VkSurfaceCapabilitiesKHR SurfaceCapabilities {};
    VkSwapchainCreateInfoKHR SwapchainCreateInfo {};

    VkExtent2D Extent;
    VkSwapchainKHR Swapchain;

    uint32_t SwapchainImageCount = 0;
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
    VkRenderingInfo RenderingInfo {};

    VkCommandBufferBeginInfo PipelineCmdBeginInfo {};
    VkSubmitInfo PipelineCmdSubmitInfo {};

    // "Passes"
    TerrainRenderer TerrainRenderer;

public:
    InferusRenderer() = default;
    ~InferusRenderer() = default;
    InferusRenderer(const InferusRenderer&) = delete;
    InferusRenderer& operator=(const InferusRenderer&) = delete;

    InferusResult Create();
    void Destroy();

    void Render();

    void Resize(uint32_t Width, uint32_t Height);

private:
    void RefreshExtent();

    void DestroySwapchain(VkSwapchainKHR OldSwapchain);
    void RecreateSwapchain(VkSwapchainKHR OldSwapchain);
    void CleanupSwapchainImages();

    void QuerySurfaceCapabilities();
};
