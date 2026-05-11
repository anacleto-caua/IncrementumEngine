#include "ImGuiPass.hpp"

#include <array>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "Engine/Core/WindowSDL.hpp"

namespace ImGuiPass {
    void NewFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    IncResult Create() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // TODO:
        // Check available styles, fonts and setup style

        ImGui_ImplSDL3_InitForVulkan(Window::SdlWindow);

        std::array<VkFormat, 1> color_attachment_formats = { VulkanContext::SurfaceFormat.format };

        VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = {},
            .colorAttachmentCount = color_attachment_formats.size(),
            .pColorAttachmentFormats = color_attachment_formats.data(),
            .depthAttachmentFormat = RendererConfig::DepthBuffer::Format,
            .stencilAttachmentFormat = RendererConfig::DepthBuffer::Format
        };

        ImGui_ImplVulkan_PipelineInfo pipeline_info {};
        pipeline_info.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

        ImGui_ImplVulkan_InitInfo vk_init_info {};
        vk_init_info.ApiVersion = VK_API_VERSION_1_4;
        vk_init_info.Instance = VulkanContext::Instance;
        vk_init_info.PhysicalDevice = VulkanContext::PhysicalDevice;
        vk_init_info.Device = VulkanContext::Device;
        vk_init_info.QueueFamily = VulkanContext::Graphics.Index;
        vk_init_info.Queue = VulkanContext::Graphics.Queue;
        vk_init_info.PipelineInfoMain = pipeline_info;
        // vk_init_info.DescriptorPool; // Leave it alone so the backend creates one with .DescriptorPoolSize
        vk_init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        vk_init_info.MinImageCount = VulkanContext::QuerySurfaceCapabilities().minImageCount;
        vk_init_info.ImageCount = Renderer::Swapchain::ImageCount;
        vk_init_info.UseDynamicRendering = true;
        vk_init_info.MinAllocationSize = 1024 * 1024; // To satisfaz zealous best practices validation layer and waste a little memory.

        ImGui_ImplVulkan_Init(&vk_init_info);
        NewFrame();

        return IncResult::SUCCESS;
    }

    void Destroy() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void Render(VkCommandBuffer cmd) {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        NewFrame();
    }

    void ProcessEvent(SDL_Event event) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}
