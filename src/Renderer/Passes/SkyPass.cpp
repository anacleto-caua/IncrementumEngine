#include "SkyPass.hpp"

#include <array>
#include <cmath>

#include <imgui.h>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"
#include "Renderer/Vk/ShaderSpecializationBuilder.hpp"

IncResult SkyPass::Init() {
    // Seed the sliders from the current direction so they start in sync with GRenderer.SunDirection.
    vec3 dir = GRenderer.SunDirection;
    SunElevationDegrees = math::degrees(std::asin(dir.y));
    SunAzimuthDegrees = math::degrees(std::atan2(dir.z, dir.x));

    // Pipeline layout - Set 0 only, its own VkPipelineLayout object even though it reuses the
    // same set layout TerrainPass does, matching that pass's precedent.
    VkPipelineLayoutCreateInfo sky_pipeline_layout_create_info {};
    sky_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    sky_pipeline_layout_create_info.setLayoutCount = 1;
    sky_pipeline_layout_create_info.pSetLayouts = &DescriptorManager::GlobalLayout;
    sky_pipeline_layout_create_info.pushConstantRangeCount = 0;
    sky_pipeline_layout_create_info.pPushConstantRanges = nullptr;

    VK_CHECK(
        vkCreatePipelineLayout(
            VkVault::Device,
            &sky_pipeline_layout_create_info,
            nullptr,
            &SkyPipelineLayout
        ),
        "sky pipeline layout creation failed"
    );

    // TODO: This should be tracked engine wise (I think)
    std::array<VkDynamicState, 2> dynamic_states = {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info {};
    dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.pNext = nullptr;
    dynamic_state_create_info.dynamicStateCount = static_cast<u32>(dynamic_states.size());
    dynamic_state_create_info.pDynamicStates = dynamic_states.data();

    VkPipelineRenderingCreateInfo rendering_create_info {};
    rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_create_info.pNext = nullptr;
    rendering_create_info.colorAttachmentCount = static_cast<u32>(VkVault::ColorAttachmentFormats.size());
    rendering_create_info.pColorAttachmentFormats = VkVault::ColorAttachmentFormats.data();
    rendering_create_info.depthAttachmentFormat = Renderer::DepthBufferFormat;
    rendering_create_info.stencilAttachmentFormat = Renderer::DepthBufferFormat;

    auto vertex_input_state = PipelineDefaults::DefaultPipelineVertexInputStateCreateInfo();
    auto input_assembly_state = PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo();
    auto viewport_state = PipelineDefaults::DefaultPipelineViewportStateCreateInfo();
    auto rasterization_state = PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo();
    auto multisample_state = PipelineDefaults::DefaultPipelineMultisampleStateCreateInfo();
    auto colorblend_state = PipelineDefaults::DefaultPipelineColorBlendStateCreateInfo();

    // Sky must never write depth (it's drawn at the far plane, behind everything already drawn)
    // and must be rejected wherever opaque geometry already wrote a closer depth - LESS_OR_EQUAL
    // instead of the default LESS since the sky's own depth sits exactly at 1.0.
    auto depth_stencil_state = PipelineDefaults::DefaultPipelineDepthStencilStateCreateInfo();
    depth_stencil_state.depthWriteEnable = VK_FALSE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo sky_pipeline_create_info {};
    sky_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    sky_pipeline_create_info.pVertexInputState = &vertex_input_state;
    sky_pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    sky_pipeline_create_info.pViewportState = &viewport_state;
    sky_pipeline_create_info.pRasterizationState = &rasterization_state;
    sky_pipeline_create_info.pMultisampleState = &multisample_state;
    sky_pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    sky_pipeline_create_info.pColorBlendState = &colorblend_state;
    sky_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    sky_pipeline_create_info.layout = SkyPipelineLayout;
    sky_pipeline_create_info.basePipelineIndex = -1;
    sky_pipeline_create_info.pNext = &rendering_create_info;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    std::vector<u32> shader_buffer;
    shader_buffer.reserve(4096);

    VkPipelineShaderStageCreateInfo vert_shader;
    INC_CHECK(
        CreateShaderStage(
            VK_SHADER_STAGE_VERTEX_BIT,
            "shaders/sky.vert.spv",
            shader_buffer,
            vert_shader
        ),
        "sky vertex shader creation failed"
    );

    VkPipelineShaderStageCreateInfo frag_shader;
    SpecializationBuilder frag_shader_spec_builder;
    frag_shader_spec_builder
        .AddConstant(0, PrimarySteps)
        .AddConstant(1, SecondarySteps);

    INC_CHECK(
        CreateShaderStage(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            "shaders/sky.frag.spv",
            shader_buffer,
            frag_shader
        ),
        "sky fragment shader creation failed"
    );
    frag_shader.pSpecializationInfo = frag_shader_spec_builder.Build();

    shader_stages.push_back(vert_shader);
    shader_stages.push_back(frag_shader);

    sky_pipeline_create_info.stageCount = static_cast<u32>(shader_stages.size());
    sky_pipeline_create_info.pStages = shader_stages.data();

    VK_CHECK(
        vkCreateGraphicsPipelines(
            VkVault::Device,
            VK_NULL_HANDLE,
            1,
            &sky_pipeline_create_info,
            nullptr,
            &SkyPipeline
        ),
        "sky pipeline creation failed."
    );

    for (auto shader_stage : shader_stages) {
        if (shader_stage.module) { vkDestroyShaderModule(VkVault::Device, shader_stage.module, nullptr); }
    }

    return IncResult::SUCCESS;
}

void SkyPass::Destroy() {
    if (SkyPipeline) { vkDestroyPipeline(VkVault::Device, SkyPipeline, nullptr); }
    if (SkyPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, SkyPipelineLayout, nullptr); }
}

void SkyPass::Render() {
    OutSkyData();

    VkCommandBuffer& cmd = GRenderer.FrameContext.DrawCommand;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SkyPipeline);

    VkDescriptorSet set0 = GRenderer.GlobalDescriptors.Sets[GRenderer.FrameContext.FrameInFlightIndex];
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SkyPipelineLayout,
        DescriptorMap::Global::SetIndex, 1, &set0, 0, nullptr
    );

    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void SkyPass::OutSkyData() {
    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;
        changed |= ImGui::SliderFloat("Sun Azimuth", &SunAzimuthDegrees, -180.0f, 180.0f);
        changed |= ImGui::SliderFloat("Sun Elevation", &SunElevationDegrees, 0.0f, 180.0f);

        if (changed) {
            f32 azimuth = math::radians(SunAzimuthDegrees);
            f32 elevation = math::radians(SunElevationDegrees);
            GRenderer.SunDirection = math::normalize(vec3(
                std::cos(elevation) * std::cos(azimuth),
                std::sin(elevation),
                std::cos(elevation) * std::sin(azimuth)
            ));
        }
    }
}
