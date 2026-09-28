#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core/Math.hpp"
#include "Renderer/RendererConstants.hpp"
#include "Renderer/Resources/Image.hpp"
#include "Renderer/Resources/Buffer.hpp"
#include "Renderer/Resources/ImageView.hpp"
#include "Renderer/Vk/BinarySemaphore.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"
#include "Renderer/Tools/TransferPipe.hpp"
#include "Renderer/Tools/ComputePipe.hpp"
#include "Renderer/Passes/TerrainPass.hpp"
#include "Renderer/Passes/PropPass.hpp"
#include "Renderer/Passes/SkyPass.hpp"
#include "Renderer/Passes/TextPass.hpp"
#include "Renderer/Passes/PostPass.hpp"
#include "Renderer/Passes/ImGuiPass.hpp"

struct Camera;

class Renderer {
public:
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = RendererConstants::MAX_FRAMES_IN_FLIGHT;
    static constexpr VkFormat DepthBufferFormat = RendererConstants::DepthBufferFormat;

    Camera* CurrentCamera = nullptr;

    // Any future lighting system shall feed from this
    vec3 SunDirection = math::normalize(vec3(0.0f, 0.0f, -1.0f));

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

    // ImageCount is read by ImGuiPass's Vulkan backend init; Width/Height by TextPass to convert
    // its screen-space pixel coordinates to NDC - the actual GPU-side swapchain resolution, not
    // EngineConfig::Width/Height (which can be a frame stale relative to a live resize).
    struct SwapchainState {
        u32 ImageCount = 0;
        u32 Width = 0;
        u32 Height = 0;
    };
    SwapchainState Swapchain;

    IncResult Init();
    void Destroy();

    void Frame();

    void Resize(i32 width, i32 height);
    void BindCamera(Camera* camera);

    // The single-sample resolve target PostPass reads - recreated on resize (PostPass is told via
    // RebindSceneColor(), so never cache this handle across frames).
    VkImageView GetResolvedSceneColorView();

    // Owned, not independent globals - completes the same ownership shape as TransferPipe below:
    // real members instead of raw pointers into globals declared elsewhere. Reached ambiently
    // via the GTerrainPass/GImGuiPass aliases in Game/Game.hpp.
    TerrainPass TerrainPass;
    PropPass PropPass;
    SkyPass SkyPass;
    PostPass PostPass;
    TextPass TextPass;
    ImGuiPass ImGuiPass;

private:
    // Ordered lists - this order IS the render order. Adding a pass is one line in Init(), not
    // three edits across Init()/Destroy()/Frame(). Each frame has two rendering scopes:
    //  - ScenePasses draw into the HDR, multisampled scene targets (their pipelines use
    //    PipelineDefaults::Scene* state);
    //  - OverlayPasses draw onto the swapchain after the scene is resolved (Overlay* state),
    //    starting with PostPass's tonemap.
    // Passes = both, concatenated in that order - what Init()/Destroy()/transfers loop over.
    std::vector<Pass*> ScenePasses;
    std::vector<Pass*> OverlayPasses;
    std::vector<Pass*> Passes;

    // MAX_SUBMITS raised from the class template's default (32) to 64: this pile absorbs
    // TransferPipe::AcquirePending()'s one-submission-per-pending-reacquire writes (folded in
    // during Renderer::Frame(), before the frame's own draw-command submission) plus that
    // guaranteed follow-up submission. A high-throughput streaming burst can fill this pile with
    // re-acquires alone, leaving no room for the mandatory follow-up - bump this if a similar
    // burst source overflows it again (distinct from TransferPipe's own internal submission piles).
    SubmissionPile<QueueRole::Graphics, 64> SubmissionPile;

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
        alignas (16) mat4 InverseViewProjection;
        alignas (16) vec3 SunDirection;
    };
    VkPipelineLayout GlobalDescriptorsBaseLayout = VK_NULL_HANDLE;
    std::array<BufferId, MAX_FRAMES_IN_FLIGHT> SceneGlobalsBuffer;

    IncResult InitGlobalDescriptors();
    void DestroyGlobalDescriptors();

    // Rendering info recomputed on init/resize, bound every frame - one set per scope.
    VkRect2D Scissor {};
    VkViewport Viewport {};
    VkRenderingAttachmentInfo SceneColorAttachment {};  // MSAA HDR color, resolved on end
    VkRenderingAttachmentInfo SceneDepthAttachment {};
    VkRenderingInfo SceneRenderingInfo {};
    VkRenderingAttachmentInfo OverlayColorAttachment {};  // the swapchain image, no depth
    VkRenderingInfo OverlayRenderingInfo {};

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

    // Scene targets - swapchain-sized, single instances shared by every frame in flight (each
    // frame's barriers order its use after the previous frame's on the same queue). All three
    // are fully cleared/overwritten every frame, so they're transitioned from UNDEFINED each
    // time and never need an initial layout setup of their own.
    ImageId SceneColorMsaaImage;        // SceneSampleCount samples, rendered into, never read
    ImageViewId SceneColorMsaaView;
    ImageId SceneColorResolvedImage;    // 1 sample - MSAA resolve destination, read by PostPass
    ImageViewId SceneColorResolvedView;
    ImageId DepthBufferImage;           // SceneSampleCount samples, scene scope only
    ImageViewId DepthBufferImageView;
    VkImageSubresourceRange DepthBufferRange = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    IncResult InitSceneTargets(u32 width, u32 height);
    void DestroySceneTargets();

public:
    TransferPipe TransferPipe;
    ComputePipe ComputePipe;
};
