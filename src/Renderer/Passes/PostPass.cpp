#include "PostPass.hpp"

#include <vector>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

IncResult PostPass::Init() {
    // post.frag reads with texelFetch at the exact pixel (scene target and swapchain are the same
    // size), so the sampler's filtering never actually applies - nearest is just the honest choice.
    VkSamplerCreateInfo sampler_info {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VK_CHECK(
        vkCreateSampler(VkVault::Device, &sampler_info, nullptr, &SceneColorSampler),
        "post pass sampler creation failed"
    );

    SceneColorSet = DescriptorManager::AllocateSet(DescriptorManager::PostLayout);
    RebindSceneColor();

    VkPushConstantRange exposure_range {};
    exposure_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    exposure_range.offset = 0;
    exposure_range.size = sizeof(f32);

    VkPipelineLayoutCreateInfo layout_info {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &DescriptorManager::PostLayout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &exposure_range;

    VK_CHECK(
        vkCreatePipelineLayout(VkVault::Device, &layout_info, nullptr, &PostPipelineLayout),
        "post pipeline layout creation failed"
    );

    auto dynamic_state_create_info = PipelineDefaults::DefaultPipelineDynamicStateCreateInfo();
    auto rendering_create_info = PipelineDefaults::OverlayRenderingCreateInfo();
    auto vertex_input_state = PipelineDefaults::DefaultPipelineVertexInputStateCreateInfo();
    auto input_assembly_state = PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo();
    auto viewport_state = PipelineDefaults::DefaultPipelineViewportStateCreateInfo();
    auto rasterization_state = PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo();
    auto multisample_state = PipelineDefaults::DefaultPipelineMultisampleStateCreateInfo();

    // Fullscreen overwrite: no depth attachment in the overlay scope, and no blending - every
    // swapchain pixel is written exactly once, here, before Text/ImGui blend on top.
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state {};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable = VK_FALSE;
    depth_stencil_state.depthWriteEnable = VK_FALSE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState opaque_attachment {};
    opaque_attachment.blendEnable = VK_FALSE;
    opaque_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorblend_state {};
    colorblend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorblend_state.attachmentCount = 1;
    colorblend_state.pAttachments = &opaque_attachment;

    VkGraphicsPipelineCreateInfo pipeline_info {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_create_info;
    pipeline_info.pVertexInputState = &vertex_input_state;
    pipeline_info.pInputAssemblyState = &input_assembly_state;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization_state;
    pipeline_info.pMultisampleState = &multisample_state;
    pipeline_info.pDepthStencilState = &depth_stencil_state;
    pipeline_info.pColorBlendState = &colorblend_state;
    pipeline_info.pDynamicState = &dynamic_state_create_info;
    pipeline_info.layout = PostPipelineLayout;
    pipeline_info.basePipelineIndex = -1;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    std::vector<u32> shader_buffer;
    shader_buffer.reserve(4096);

    VkPipelineShaderStageCreateInfo vert_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/post.vert.spv", shader_buffer, vert_shader), "post vertex shader creation failed");

    VkPipelineShaderStageCreateInfo frag_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/post.frag.spv", shader_buffer, frag_shader), "post fragment shader creation failed");

    shader_stages.push_back(vert_shader);
    shader_stages.push_back(frag_shader);

    pipeline_info.stageCount = static_cast<u32>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();

    VK_CHECK(
        vkCreateGraphicsPipelines(VkVault::Device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &PostPipeline),
        "post pipeline creation failed"
    );

    for (auto shader_stage : shader_stages) {
        if (shader_stage.module) { vkDestroyShaderModule(VkVault::Device, shader_stage.module, nullptr); }
    }

    return IncResult::SUCCESS;
}

void PostPass::Destroy() {
    if (PostPipeline) { vkDestroyPipeline(VkVault::Device, PostPipeline, nullptr); }
    if (PostPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, PostPipelineLayout, nullptr); }
    if (SceneColorSampler) { vkDestroySampler(VkVault::Device, SceneColorSampler, nullptr); }
}

void PostPass::RebindSceneColor() {
    VkDescriptorImageInfo image_info {};
    image_info.sampler = SceneColorSampler;
    image_info.imageView = GRenderer.GetResolvedSceneColorView();
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = SceneColorSet;
    write.dstBinding = DescriptorMap::Post::Binding_SceneColor;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(VkVault::Device, 1, &write, 0, nullptr);
}

void PostPass::Render() {
    VkCommandBuffer& cmd = GRenderer.FrameContext.DrawCommand;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PostPipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PostPipelineLayout,
        DescriptorMap::Post::SetIndex, 1, &SceneColorSet, 0, nullptr
    );
    vkCmdPushConstants(cmd, PostPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(f32), &Exposure);

    vkCmdDraw(cmd, 6, 1, 0, 0);
}
