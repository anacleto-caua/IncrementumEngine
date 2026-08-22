#include "PropPass.hpp"

#include <array>
#include <cstddef>
#include <algorithm>

#include <imgui.h>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Tools/DebugPanel.hpp"
#include "Renderer/Resources/ModelLoader.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

IncResult PropPass::LoadModel(TerrainManager::PropModel model, const char* path, vec3 base_color) {
    ModelLoader::Model cpu_model;
    INC_CHECK(ModelLoader::LoadObj(path, cpu_model), "failed to load prop model {}", path);

    u32 index = static_cast<u32>(model);
    ModelResource& resource = Models[index];
    resource.IndexCount = static_cast<u32>(cpu_model.Indices.size());
    resource.BaseColor = base_color;

    Buffer::CreateInfo vertex_buffer_info = {
        .Size = cpu_model.Vertices.size() * sizeof(ModelLoader::Vertex),
        .Type = Buffer::Type::VERTEX,
    };
    INC_CHECK(Buffers.Add(vertex_buffer_info, resource.VertexBuffer), "prop vertex buffer creation failed for {}", path);

    Buffer::CreateInfo index_buffer_info = {
        .Size = cpu_model.Indices.size() * sizeof(u32),
        .Type = Buffer::Type::INDEX,
    };
    INC_CHECK(Buffers.Add(index_buffer_info, resource.IndexBuffer), "prop index buffer creation failed for {}", path);

    GTransferPipe.QueueBufferUpload(resource.VertexBuffer, 0, cpu_model.Vertices.data(), vertex_buffer_info.Size);
    GTransferPipe.QueueBufferUpload(resource.IndexBuffer, 0, cpu_model.Indices.data(), index_buffer_info.Size);

    return IncResult::SUCCESS;
}

IncResult PropPass::Init() {
    // Model table - fixed list for v1; no real sourced art yet, so both models are hand-authored
    // placeholders under assets/models/, swappable later without code changes here. Both uploads
    // are queued above and flushed together by the one LazySubmit() below - startup-only, so the
    // blocking wait is fine (same posture as TerrainPass::PlaneMeshResource::Upload()).
    INC_CHECK(LoadModel(TerrainManager::PropModel::Tree, "assets/models/tree.obj", vec3(0.30f, 0.45f, 0.15f)), "tree model load failed");
    INC_CHECK(LoadModel(TerrainManager::PropModel::Rock, "assets/models/rock.obj", vec3(0.45f, 0.43f, 0.40f)), "rock model load failed");
    GTransferPipe.LazySubmit();

    // Per-model, per-frame-in-flight instance SSBO + descriptor set
    {
        Buffer::CreateInfo instance_buffer_info = {
            .Size = MaxInstancesPerModel * sizeof(GpuInstance),
            .Type = Buffer::Type::SSBO,
        };

        for (u32 model = 0; model < ModelCount; model++) {
            for (u32 frame = 0; frame < RendererConstants::MAX_FRAMES_IN_FLIGHT; frame++) {
                INC_CHECK(Buffers.Add(instance_buffer_info, InstanceBuffers[model][frame]), "prop instance buffer creation failed");
            }

            DescriptorManager::AllocateSets(
                DescriptorManager::PropPerFrameLayout,
                RendererConstants::MAX_FRAMES_IN_FLIGHT,
                DescriptorSets[model].data()
            );

            for (u32 frame = 0; frame < RendererConstants::MAX_FRAMES_IN_FLIGHT; frame++) {
                auto instance_buffer_value = Buffers.Get(InstanceBuffers[model][frame]);
                VkDescriptorBufferInfo instance_buffer_descriptor {};
                instance_buffer_descriptor.buffer = instance_buffer_value->Handle;
                instance_buffer_descriptor.offset = 0;
                instance_buffer_descriptor.range = instance_buffer_value->Size;

                VkWriteDescriptorSet instance_ssbo_write {};
                instance_ssbo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                instance_ssbo_write.dstSet = DescriptorSets[model][frame];
                instance_ssbo_write.dstBinding = DescriptorMap::PropPerFrame::Binding_InstanceSSBO;
                instance_ssbo_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                instance_ssbo_write.descriptorCount = 1;
                instance_ssbo_write.pBufferInfo = &instance_buffer_descriptor;

                vkUpdateDescriptorSets(VkVault::Device, 1, &instance_ssbo_write, 0, nullptr);
            }

            StagingInstances[model].reserve(MaxInstancesPerModel);
        }
    }

    // Pipeline layout
    std::array<VkDescriptorSetLayout, 2> pipeline_layouts = {
        DescriptorManager::GlobalLayout,
        DescriptorManager::PropPerFrameLayout
    };

    VkPipelineLayoutCreateInfo prop_pipeline_layout_create_info {};
    prop_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    prop_pipeline_layout_create_info.setLayoutCount = static_cast<u32>(pipeline_layouts.size());
    prop_pipeline_layout_create_info.pSetLayouts = pipeline_layouts.data();
    prop_pipeline_layout_create_info.pushConstantRangeCount = 0;
    prop_pipeline_layout_create_info.pPushConstantRanges = nullptr;

    VK_CHECK(
        vkCreatePipelineLayout(VkVault::Device, &prop_pipeline_layout_create_info, nullptr, &PropPipelineLayout),
        "prop pipeline layout creation failed"
    );

    // Real (non-empty) vertex input state - the first pipeline in the codebase to need one, since
    // terrain generates its vertices procedurally in-shader (PipelineDefaults's default vertex
    // input state deliberately declares zero bindings/attributes for that reason).
    VkVertexInputBindingDescription vertex_binding {};
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(ModelLoader::Vertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> vertex_attributes {{
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(ModelLoader::Vertex, Position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(ModelLoader::Vertex, Normal) }
    }};

    VkPipelineVertexInputStateCreateInfo vertex_input_state {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = 1;
    vertex_input_state.pVertexBindingDescriptions = &vertex_binding;
    vertex_input_state.vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size());
    vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.data();

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
    auto depth_stencil_state = PipelineDefaults::DefaultPipelineDepthStencilStateCreateInfo();
    auto colorblend_state = PipelineDefaults::DefaultPipelineColorBlendStateCreateInfo();

    VkGraphicsPipelineCreateInfo prop_pipeline_create_info {};
    prop_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    prop_pipeline_create_info.pVertexInputState = &vertex_input_state;
    prop_pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    prop_pipeline_create_info.pViewportState = &viewport_state;
    prop_pipeline_create_info.pRasterizationState = &rasterization_state;
    prop_pipeline_create_info.pMultisampleState = &multisample_state;
    prop_pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    prop_pipeline_create_info.pColorBlendState = &colorblend_state;
    prop_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    prop_pipeline_create_info.layout = PropPipelineLayout;
    prop_pipeline_create_info.basePipelineIndex = -1;
    prop_pipeline_create_info.pNext = &rendering_create_info;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    std::vector<u32> shader_buffer;
    shader_buffer.reserve(4096);

    VkPipelineShaderStageCreateInfo vert_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/prop.vert.spv", shader_buffer, vert_shader), "prop vertex shader creation failed");

    VkPipelineShaderStageCreateInfo frag_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/prop.frag.spv", shader_buffer, frag_shader), "prop fragment shader creation failed");

    shader_stages.push_back(vert_shader);
    shader_stages.push_back(frag_shader);

    prop_pipeline_create_info.stageCount = static_cast<u32>(shader_stages.size());
    prop_pipeline_create_info.pStages = shader_stages.data();

    VK_CHECK(
        vkCreateGraphicsPipelines(VkVault::Device, VK_NULL_HANDLE, 1, &prop_pipeline_create_info, nullptr, &PropPipeline),
        "prop pipeline creation failed"
    );

    for (auto shader_stage : shader_stages) {
        if (shader_stage.module) { vkDestroyShaderModule(VkVault::Device, shader_stage.module, nullptr); }
    }

    return IncResult::SUCCESS;
}

void PropPass::Destroy() {
    for (u32 model = 0; model < ModelCount; model++) {
        for (auto& buffer : InstanceBuffers[model]) { Buffers.Del(buffer); }
        Buffers.Del(Models[model].VertexBuffer);
        Buffers.Del(Models[model].IndexBuffer);
    }

    if (PropPipeline) { vkDestroyPipeline(VkVault::Device, PropPipeline, nullptr); }
    if (PropPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, PropPipelineLayout, nullptr); }
}

void PropPass::FrameSensibleTransfers() {
    using namespace TerrainManager;

    for (u32 model = 0; model < ModelCount; model++) { StagingInstances[model].clear(); }

    for (u32 i = 0; i < CurrentlyActiveChunks; i++) {
        const ChunkInstanceData& chunk = ChunkDrawList[i];
        const PropPlacement& props = GetPropsForLayer(chunk.TextureLayer);

        for (u32 p = 0; p < props.Count; p++) {
            const PropInstance& instance = props.Instances[p];
            u32 model_index = static_cast<u32>(instance.Model);

            GpuInstance gpu_instance {};
            gpu_instance.PositionAndRotation = vec4(instance.WorldPosition, instance.YRotation);
            gpu_instance.ScaleAndColor = vec4(instance.Scale, Models[model_index].BaseColor);

            StagingInstances[model_index].push_back(gpu_instance);
        }
    }

    u32 frame_index = GRenderer.FrameContext.FrameInFlightIndex;

    // vkCmdUpdateBuffer caps a single call at 65536 bytes (a hard Vulkan limit) and
    // MaxInstancesPerModel's worst case exceeds that, so this uploads in <=65536-byte chunks
    // rather than switching upload mechanisms.
    constexpr u32 MaxInstancesPerUpdateCall = 65536 / sizeof(GpuInstance);

    for (u32 model = 0; model < ModelCount; model++) {
        u32 count = static_cast<u32>(StagingInstances[model].size());
        if (count == 0) { continue; }

        auto buffer = Buffers.Get(InstanceBuffers[model][frame_index]);
        const GpuInstance* data = StagingInstances[model].data();

        u32 uploaded = 0;
        while (uploaded < count) {
            u32 chunk_count = std::min(count - uploaded, MaxInstancesPerUpdateCall);
            vkCmdUpdateBuffer(
                GRenderer.FrameContext.DrawCommand,
                buffer->Handle,
                static_cast<VkDeviceSize>(uploaded) * sizeof(GpuInstance),
                static_cast<VkDeviceSize>(chunk_count) * sizeof(GpuInstance),
                data + uploaded
            );
            uploaded += chunk_count;
        }
    }
}

void PropPass::Render() {
    OutPropData();

    VkCommandBuffer& cmd = GRenderer.FrameContext.DrawCommand;
    u32 frame_index = GRenderer.FrameContext.FrameInFlightIndex;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PropPipeline);

    for (u32 model = 0; model < ModelCount; model++) {
        u32 count = static_cast<u32>(StagingInstances[model].size());
        if (count == 0) { continue; }

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &Buffers.Get(Models[model].VertexBuffer)->Handle, &offset);
        vkCmdBindIndexBuffer(cmd, Buffers.Get(Models[model].IndexBuffer)->Handle, 0, VK_INDEX_TYPE_UINT32);

        std::array<VkDescriptorSet, 2> sets_to_bind = {
            GRenderer.GlobalDescriptors.Sets[frame_index],  // Set 0
            DescriptorSets[model][frame_index]              // Set 1
        };
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            PropPipelineLayout,
            DescriptorMap::Global::SetIndex,
            static_cast<u32>(sets_to_bind.size()),
            sets_to_bind.data(),
            0,
            nullptr
        );

        vkCmdDrawIndexed(cmd, Models[model].IndexCount, count, 0, 0, 0);
    }
}

// Per-model instance counts - StagingInstances is already bucketed by FrameSensibleTransfers()
// every frame, so this just reads it.
void PropPass::OutPropData() {
    static const char* model_names[ModelCount] = { "Tree", "Rock" };
    static_assert(ModelCount == 2, "update model_names above if the PropModel enum changes");

    if (DebugPanel::BeginSection(DebugPanel::Section::Props)) {
        u32 total = 0;
        for (u32 model = 0; model < ModelCount; model++) {
            u32 count = static_cast<u32>(StagingInstances[model].size());
            ImGui::Text("%s: %u", model_names[model], count);
            total += count;
        }
        ImGui::Text("Total: %u (max %u/model)", total, MaxInstancesPerModel);

        DebugPanel::EndSection();
    }
}
