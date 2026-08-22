#include "ImGuiPass.hpp"

#include <array>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Engine/Core/WindowSDL.hpp"
#include "Renderer/Tools/DebugPanel.hpp"

void ImGuiPass::NewFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

IncResult ImGuiPass::Init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // TODO:
    // Check available styles, fonts and setup style

    ImGui_ImplSDL3_InitForVulkan(Window::SdlWindow);

    VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .pNext = nullptr,
        .viewMask = {},
        .colorAttachmentCount = VkVault::ColorAttachmentFormats.size(),
        .pColorAttachmentFormats = VkVault::ColorAttachmentFormats.data(),
        .depthAttachmentFormat = Renderer::DepthBufferFormat,
        .stencilAttachmentFormat = Renderer::DepthBufferFormat
    };

    ImGui_ImplVulkan_PipelineInfo pipeline_info {};
    pipeline_info.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

    ImGui_ImplVulkan_InitInfo vk_init_info {};
    vk_init_info.ApiVersion = VK_API_VERSION_1_4;
    vk_init_info.Instance = VkVault::Instance;
    vk_init_info.PhysicalDevice = VkVault::PhysicalDevice;
    vk_init_info.Device = VkVault::Device;
    vk_init_info.QueueFamily = VkVault::Queues[QueueRole::Graphics].FamilyIndex;
    vk_init_info.Queue = VkVault::Queues[QueueRole::Graphics].Queue;
    vk_init_info.PipelineInfoMain = pipeline_info;
    // vk_init_info.DescriptorPool; // Leave it alone so the backend creates one with .DescriptorPoolSize
    vk_init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    vk_init_info.MinImageCount = VkVault::QuerySurfaceCapabilities().minImageCount;
    vk_init_info.ImageCount = GRenderer.Swapchain.ImageCount;
    vk_init_info.UseDynamicRendering = true;
    vk_init_info.MinAllocationSize = 1024 * 1024; // To satisfaz zealous best practices validation layer and waste a little memory.

    ImGui_ImplVulkan_Init(&vk_init_info);
    NewFrame();

    return IncResult::SUCCESS;
}

void ImGuiPass::Destroy() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiPass::Render() {
    // Renders into ImGui's own implicit default window (the same "Debug" window every other
    // pass's Out*Data() populates, no explicit Begin/End of its own - this is the one file
    // positioned to know cross-pass renderer state like the live swapchain resolution and current
    // sun direction without depending on any one specific pass).
    if (DebugPanel::BeginSection(DebugPanel::Section::Renderer)) {
        ImGui::Text("Swapchain: %u x %u (%u images)", GRenderer.Swapchain.Width, GRenderer.Swapchain.Height, GRenderer.Swapchain.ImageCount);
        ImGui::Text("Frame in flight: %u / %u", GRenderer.FrameContext.FrameInFlightIndex, Renderer::MAX_FRAMES_IN_FLIGHT);
        ImGui::Text("Sun Direction: (%.2f, %.2f, %.2f)", static_cast<f64>(GRenderer.SunDirection.x), static_cast<f64>(GRenderer.SunDirection.y), static_cast<f64>(GRenderer.SunDirection.z));

        // Was console-log-only until now (VkVault.cpp's own TODO asked for this) - which queue
        // roles happen to share a physical queue family is GPU/driver-dependent, so seeing it live
        // is more useful than a one-shot startup log line.
        if (ImGui::TreeNodeEx("Queues", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Unique families: %u", VkVault::UniqueFamilyCount);
            auto queue_row = [](const char* name, QueueRole role) {
                const QueueValue& queue = VkVault::Queues[role];
                ImGui::Text("%-10s family %u (resource slot %u)", name, queue.FamilyIndex, static_cast<u32>(queue.UniqueFamilyId));
            };
            queue_row("Graphics", QueueRole::Graphics);
            queue_row("Present", QueueRole::Present);
            queue_row("Transfer", QueueRole::Transfer);
            queue_row("Compute", QueueRole::Compute);
            ImGui::TreePop();
        }

        DebugPanel::EndSection();
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), GRenderer.FrameContext.DrawCommand);
    NewFrame();
}

void ImGuiPass::ProcessEvent(SDL_Event event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}
