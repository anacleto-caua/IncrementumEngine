#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core/Math.hpp"
#include "Renderer/RendererConstants.hpp"
#include "Renderer/Resources/Image.hpp"
#include "Renderer/Resources/Buffer.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/BinarySemaphore.hpp"
#include "Renderer/Resources/ImageView.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"
#include "Renderer/Resources/TransferPipe.hpp"
#include "Renderer/Passes/TerrainPass.hpp"
#include "Renderer/Passes/ImGuiPass.hpp"

// Forward-declared, not included: only ever used here as a pointer, and full inclusion would be
// circular (Camera.hpp reads GEngine.Config, transitively reachable from here).
struct Camera;

class Renderer {
public:
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = RendererConstants::MAX_FRAMES_IN_FLIGHT;
    static constexpr VkFormat DepthBufferFormat = RendererConstants::DepthBufferFormat;

    Camera* CurrentCamera = nullptr;

    // Per-frame data shared with passes - read directly every frame by TerrainPass/ImGuiPass.
    struct RenderFrameData {
        u32 FrameInFlightIndex = 0;
        u32 ImageViewIndex = 0;
        VkCommandBuffer DrawCommand = VK_NULL_HANDLE;
    };
    RenderFrameData FrameContext;

    // Set 0 descriptor sets - read directly by every pass's Render().
    struct GlobalDescriptorsState {
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> Sets = { VK_NULL_HANDLE };
    };
    GlobalDescriptorsState GlobalDescriptors;

    // Only ImageCount is read outside this class (ImGuiPass's Vulkan backend init).
    struct SwapchainState {
        u32 ImageCount = 0;
    };
    SwapchainState Swapchain;

    IncResult Init();
    void Destroy();

    void Frame();

    void Resize(i32 width, i32 height);
    void BindCamera(Camera* camera);

    // Owned, not independent globals - completes the same ownership shape as TransferPipe below:
    // real members instead of raw pointers into globals declared elsewhere. Reached ambiently
    // via the GTerrainPass/GImGuiPass aliases in Game/Game.hpp.
    TerrainPass TerrainPass;
    ImGuiPass ImGuiPass;

private:
    // Ordered list - this order IS the render order. Adding a pass is one line here, not
    // three edits across Init()/Destroy()/Frame(). Populated in Init() with addresses of the
    // two owned members above.
    std::vector<Pass*> Passes;

    SubmissionPile<QueueRole::Graphics> SubmissionPile;

    struct FrameData {
        CommandBufferBlock FrameBlock;
        BinarySemaphore ImageAvailable = {};
        u64 LastSignaledValue = 0;
    };
    TimelineSemaphore FrameSemaphore;
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> Frames;

    // Camera UBO - the internal half of GlobalDescriptorsState above.
    struct SceneGlobals {
        mat4 ViewProjection;
        alignas (16) vec3 CameraPosition;
    };
    VkPipelineLayout GlobalDescriptorsBaseLayout = VK_NULL_HANDLE;
    std::array<BufferId, MAX_FRAMES_IN_FLIGHT> SceneGlobalsBuffer;

    IncResult InitGlobalDescriptors();
    void DestroyGlobalDescriptors();

    // Rendering info recomputed on init/resize, bound every frame.
    VkRect2D Scissor {};
    VkViewport Viewport {};
    VkRenderingAttachmentInfo ColorAttachment {};
    VkRenderingAttachmentInfo DepthAttachment {};
    VkRenderingInfo RenderingInfo {};

    // Swapchain - the internal half of SwapchainState above.
    struct SwapchainImage {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView ImageView = VK_NULL_HANDLE;
        BinarySemaphore RenderFinished = {};
    };
    VkSwapchainCreateInfoKHR SwapchainCreateInfo {};
    VkExtent2D SwapchainExtent {};
    VkSwapchainKHR SwapchainHandle = VK_NULL_HANDLE;
    VkPresentInfoKHR SwapchainPresentInfo {};
    std::vector<SwapchainImage> SwapchainImages;

    IncResult InitSwapchain();
    void DestroySwapchain();
    IncResult ResizeSwapchain(u32 width, u32 height);
    IncResult RecreateSwapchain(VkSwapchainKHR old_swapchain);
    void DestroySwapchainKHR(VkSwapchainKHR swapchain);
    void CleanupSwapchainImages();

    // Depth buffer.
    ImageId DepthBufferImage;
    ImageViewId DepthBufferImageView;
    VkImageSubresourceRange DepthBufferRange = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    IncResult InitDepthBuffer(u32 width, u32 height);
    void DestroyDepthBuffer();
    void ResizeDepthBuffer(u32 width, u32 height);

public:
    // Owned, not an independent global - TransferPipe has real RAII sub-members (TimelineSemaphore,
    // CommandBufferBlock), so its teardown order benefits from a guaranteed owner. Reached
    // ambiently via the GTransferPipe alias in Game/Game.hpp, same as before.
    TransferPipe TransferPipe;
};
