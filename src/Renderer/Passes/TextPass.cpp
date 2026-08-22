#include "TextPass.hpp"

#include <array>
#include <vector>
#include <cstring>
#include <cstddef>
#include <algorithm>

#include <imgui.h>

#define STB_EASY_FONT_IMPLEMENTATION // no-op (every function is already `static`) but harmless -
                                      // documents intent the same way implementation.cpp does for
                                      // the other vendored single-header libs
#include <stb/stb_easy_font.h>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"

void TextPass::DrawText(const char* text, f32 x, f32 y, vec3 color, f32 scale) {
    if (StagingVertexCount >= MaxVertices) { return; }

    unsigned char rgba[4] = {
        static_cast<unsigned char>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f),
        255
    };

    u32 remaining = MaxVertices - StagingVertexCount;

    // stb_easy_font has no built-in scale parameter. Common case (scale == 1.0, the vast majority
    // of calls): write straight into StagingVertices' own remaining capacity, no allocation, no
    // copy. Only the scaled path needs a scratch buffer, since positions have to be rescaled
    // around (x, y) after stb_easy_font_print lays them out at its native size.
    if (scale == 1.0f) {
        int quads = stb_easy_font_print(
            x, y,
            const_cast<char*>(text),
            rgba,
            StagingVertices.data() + StagingVertexCount,
            static_cast<int>(remaining * sizeof(TextVertex))
        );
        if (quads > 0) { StagingVertexCount += static_cast<u32>(quads) * 4; }
        return;
    }

    std::vector<TextVertex> local(remaining);
    int quads = stb_easy_font_print(
        0.0f, 0.0f,
        const_cast<char*>(text),
        rgba,
        local.data(),
        static_cast<int>(local.size() * sizeof(TextVertex))
    );
    if (quads <= 0) { return; }

    u32 vertex_count = static_cast<u32>(quads) * 4;
    for (u32 i = 0; i < vertex_count; i++) {
        local[i].X = x + local[i].X * scale;
        local[i].Y = y + local[i].Y * scale;
    }

    std::memcpy(StagingVertices.data() + StagingVertexCount, local.data(), vertex_count * sizeof(TextVertex));
    StagingVertexCount += vertex_count;
}

IncResult TextPass::Init() {
    // Fixed quad-index pattern (0,1,2, 0,2,3 per quad, offset by 4 vertices each time) - same
    // "precompute the whole capacity's index buffer once" shape TerrainPass::PlaneMeshResource
    // uses, since every quad stb_easy_font emits shares this exact 4-vertex layout.
    {
        Buffer::CreateInfo index_buffer_info = {
            .Size = MaxIndices * sizeof(u32),
            .Type = Buffer::Type::INDEX,
        };
        INC_CHECK(Buffers.Add(index_buffer_info, IndexBuffer), "text index buffer creation failed");

        std::vector<u32> indices(MaxIndices);
        for (u32 quad = 0; quad < MaxQuads; quad++) {
            u32 base_vertex = quad * 4;
            u32 base_index = quad * 6;
            indices[base_index + 0] = base_vertex + 0;
            indices[base_index + 1] = base_vertex + 1;
            indices[base_index + 2] = base_vertex + 2;
            indices[base_index + 3] = base_vertex + 0;
            indices[base_index + 4] = base_vertex + 2;
            indices[base_index + 5] = base_vertex + 3;
        }

        GTransferPipe.QueueBufferUpload(IndexBuffer, 0, indices.data(), index_buffer_info.Size);
        GTransferPipe.LazySubmit();
    }

    // Per-frame-in-flight vertex buffer, rewritten every frame FrameSensibleTransfers() runs.
    {
        Buffer::CreateInfo vertex_buffer_info = {
            .Size = MaxVertices * sizeof(TextVertex),
            .Type = Buffer::Type::VERTEX,
        };
        for (auto& buffer : VertexBuffers) {
            INC_CHECK(Buffers.Add(vertex_buffer_info, buffer), "text vertex buffer creation failed");
        }
    }

    // No descriptor sets at all - screen-space position is computed straight from a push constant
    // (inverse screen size), the simplest pipeline layout in the codebase so far.
    VkPushConstantRange push_constant_range {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(vec2);

    VkPipelineLayoutCreateInfo text_pipeline_layout_create_info {};
    text_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    text_pipeline_layout_create_info.setLayoutCount = 0;
    text_pipeline_layout_create_info.pSetLayouts = nullptr;
    text_pipeline_layout_create_info.pushConstantRangeCount = 1;
    text_pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    VK_CHECK(
        vkCreatePipelineLayout(VkVault::Device, &text_pipeline_layout_create_info, nullptr, &TextPipelineLayout),
        "text pipeline layout creation failed"
    );

    VkVertexInputBindingDescription vertex_binding {};
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(TextVertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> vertex_attributes {{
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(TextVertex, X) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(TextVertex, Color) }
    }};

    VkPipelineVertexInputStateCreateInfo vertex_input_state {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = 1;
    vertex_input_state.pVertexBindingDescriptions = &vertex_binding;
    vertex_input_state.vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size());
    vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.data();

    // Screen-space overlay, always on top - no depth test/write, unlike every other pass's
    // pipeline (PipelineDefaults's depth-stencil state assumes real 3D depth, which text doesn't
    // have any use for).
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state {};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable = VK_FALSE;
    depth_stencil_state.depthWriteEnable = VK_FALSE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    auto dynamic_state_create_info = PipelineDefaults::DefaultPipelineDynamicStateCreateInfo();

    VkPipelineRenderingCreateInfo rendering_create_info {};
    rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_create_info.colorAttachmentCount = static_cast<u32>(VkVault::ColorAttachmentFormats.size());
    rendering_create_info.pColorAttachmentFormats = VkVault::ColorAttachmentFormats.data();
    rendering_create_info.depthAttachmentFormat = Renderer::DepthBufferFormat;
    rendering_create_info.stencilAttachmentFormat = Renderer::DepthBufferFormat;

    auto input_assembly_state = PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo();
    auto viewport_state = PipelineDefaults::DefaultPipelineViewportStateCreateInfo();
    auto rasterization_state = PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo();
    auto multisample_state = PipelineDefaults::DefaultPipelineMultisampleStateCreateInfo();
    auto colorblend_state = PipelineDefaults::DefaultPipelineColorBlendStateCreateInfo();

    VkGraphicsPipelineCreateInfo text_pipeline_create_info {};
    text_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    text_pipeline_create_info.pVertexInputState = &vertex_input_state;
    text_pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    text_pipeline_create_info.pViewportState = &viewport_state;
    text_pipeline_create_info.pRasterizationState = &rasterization_state;
    text_pipeline_create_info.pMultisampleState = &multisample_state;
    text_pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    text_pipeline_create_info.pColorBlendState = &colorblend_state;
    text_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    text_pipeline_create_info.layout = TextPipelineLayout;
    text_pipeline_create_info.basePipelineIndex = -1;
    text_pipeline_create_info.pNext = &rendering_create_info;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    std::vector<u32> shader_buffer;
    shader_buffer.reserve(4096);

    VkPipelineShaderStageCreateInfo vert_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/text.vert.spv", shader_buffer, vert_shader), "text vertex shader creation failed");

    VkPipelineShaderStageCreateInfo frag_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/text.frag.spv", shader_buffer, frag_shader), "text fragment shader creation failed");

    shader_stages.push_back(vert_shader);
    shader_stages.push_back(frag_shader);

    text_pipeline_create_info.stageCount = static_cast<u32>(shader_stages.size());
    text_pipeline_create_info.pStages = shader_stages.data();

    VK_CHECK(
        vkCreateGraphicsPipelines(VkVault::Device, VK_NULL_HANDLE, 1, &text_pipeline_create_info, nullptr, &TextPipeline),
        "text pipeline creation failed"
    );

    for (auto shader_stage : shader_stages) {
        if (shader_stage.module) { vkDestroyShaderModule(VkVault::Device, shader_stage.module, nullptr); }
    }

    return IncResult::SUCCESS;
}

void TextPass::Destroy() {
    for (auto& buffer : VertexBuffers) { Buffers.Del(buffer); }
    Buffers.Del(IndexBuffer);

    if (TextPipeline) { vkDestroyPipeline(VkVault::Device, TextPipeline, nullptr); }
    if (TextPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, TextPipelineLayout, nullptr); }
}

void TextPass::FrameSensibleTransfers() {
    if (StagingVertexCount == 0) { return; }

    u32 frame_index = GRenderer.FrameContext.FrameInFlightIndex;
    auto buffer = Buffers.Get(VertexBuffers[frame_index]);

    // Same chunked-upload shape PropPass::FrameSensibleTransfers() uses - vkCmdUpdateBuffer caps a
    // single call at 65536 bytes (a hard Vulkan limit), which MaxVertices' worst case can exceed.
    constexpr u32 MaxVerticesPerUpdateCall = 65536 / sizeof(TextVertex);

    u32 uploaded = 0;
    while (uploaded < StagingVertexCount) {
        u32 chunk_count = std::min(StagingVertexCount - uploaded, MaxVerticesPerUpdateCall);
        vkCmdUpdateBuffer(
            GRenderer.FrameContext.DrawCommand,
            buffer->Handle,
            static_cast<VkDeviceSize>(uploaded) * sizeof(TextVertex),
            static_cast<VkDeviceSize>(chunk_count) * sizeof(TextVertex),
            StagingVertices.data() + uploaded
        );
        uploaded += chunk_count;
    }
}

void TextPass::Render() {
    OutTextData();

    if (StagingVertexCount == 0) { return; }

    VkCommandBuffer& cmd = GRenderer.FrameContext.DrawCommand;
    u32 frame_index = GRenderer.FrameContext.FrameInFlightIndex;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, TextPipeline);

    vec2 inv_screen_size = {
        GRenderer.Swapchain.Width > 0 ? 1.0f / static_cast<f32>(GRenderer.Swapchain.Width) : 0.0f,
        GRenderer.Swapchain.Height > 0 ? 1.0f / static_cast<f32>(GRenderer.Swapchain.Height) : 0.0f
    };
    vkCmdPushConstants(cmd, TextPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vec2), &inv_screen_size);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &Buffers.Get(VertexBuffers[frame_index])->Handle, &offset);
    vkCmdBindIndexBuffer(cmd, Buffers.Get(IndexBuffer)->Handle, 0, VK_INDEX_TYPE_UINT32);

    u32 quad_count = StagingVertexCount / 4;
    vkCmdDrawIndexed(cmd, quad_count * 6, 1, 0, 0, 0);

    // Cleared after drawing (not at the top of the frame) so DrawText() calls made any time after
    // Engine::Frame() returns this frame start filling a fresh buffer for next frame's upload -
    // see DrawText()'s own comment on the resulting one-frame latency.
    StagingVertexCount = 0;
}

void TextPass::OutTextData() {
    if (ImGui::CollapsingHeader("Text")) {
        ImGui::Text("Quads: %u / %u", StagingVertexCount / 4, MaxQuads);
    }
}
