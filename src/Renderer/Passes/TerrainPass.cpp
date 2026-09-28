#include "TerrainPass.hpp"

#include <array>

#include <imgui.h>
#include <glm/ext.hpp>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Tools/DebugPanel.hpp"
#include "Renderer/Resources/ImageView.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"
#include "Renderer/Vk/ShaderSpecializationBuilder.hpp"

Ticket TerrainPass::HeightmapResource::QueueSlice(u32 target_layer, const void* data, u64 size) {
    return GTransferPipe.QueueImageSliceUpload(Image, target_layer, data, size);
}

IncResult TerrainPass::Init() {
    // Image for terrain heightmap
    {
        Image::CreateInfo heightmap_image_create_desc;
        heightmap_image_create_desc.Width = TerrainManager::VerticesPerEdge;
        heightmap_image_create_desc.Height = TerrainManager::VerticesPerEdge;
        heightmap_image_create_desc.ArrayLayers = TerrainManager::TotalMaxCachedChunks;
        heightmap_image_create_desc.Format = HeightmapResource::Format;
        heightmap_image_create_desc.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        heightmap_image_create_desc.OwnerQueue = QueueRole::Graphics;
        heightmap_image_create_desc.UsageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        INC_CHECK(Images.Add(heightmap_image_create_desc, Heightmap.Image), "heightmap image creation failed");
        Image* heightmap_image_value = Images.Get(Heightmap.Image);
        auto heightmap_image_view_create_info = FillImageViewCreateInfo(heightmap_image_value);
        INC_CHECK(ImageViews.Add(heightmap_image_view_create_info, Heightmap.View), "heightmap image view creation failed");

        VkSamplerCreateInfo heightmap_sampler_info {};
        heightmap_sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        heightmap_sampler_info.magFilter = VK_FILTER_LINEAR;
        heightmap_sampler_info.minFilter = VK_FILTER_LINEAR;
        heightmap_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        heightmap_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        heightmap_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        heightmap_sampler_info.anisotropyEnable = VK_FALSE;
        heightmap_sampler_info.maxAnisotropy = 1.0f;
        heightmap_sampler_info.unnormalizedCoordinates = VK_FALSE;
        heightmap_sampler_info.compareEnable = VK_FALSE;
        heightmap_sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
        heightmap_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        heightmap_sampler_info.mipLodBias = 0.0f;
        heightmap_sampler_info.minLod = 0.0f;
        heightmap_sampler_info.maxLod = 0.0f;

        VK_CHECK(
            vkCreateSampler(VkVault::Device, &heightmap_sampler_info, nullptr, &Heightmap.Sampler),
            "heightmap sampler creation failed"
        );
    }

    // Buffer for chunk draw list
    {
        Buffer::CreateInfo chunk_instance_buffer_create_info = {
            .Size = TerrainManager::TotalMaxDrawnChunks * sizeof(TerrainManager::ChunkInstanceData),
            .Type = Buffer::Type::SSBO,
        };

        for (auto& draw_list_buffer : ChunkDrawListBuffers) {
            INC_CHECK(Buffers.Add(chunk_instance_buffer_create_info, draw_list_buffer), "chunk draw list buffer creation failed");
        }
    }

    // Descriptors
    {
        // Allocate the array of Terrain Sets (Set 1)
        DescriptorManager::AllocateSets(
            DescriptorManager::PerFrameLayout,
            Renderer::MAX_FRAMES_IN_FLIGHT,
            DescriptorSets.data()
        );

        auto heightmap_image_view_value = ImageViews.Get(Heightmap.View);
        VkDescriptorImageInfo heightmap_descriptor_image_info {};
        heightmap_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        heightmap_descriptor_image_info.imageView = heightmap_image_view_value->Handle;
        heightmap_descriptor_image_info.sampler = Heightmap.Sampler;

        // Loop through each frame in flight and write both bindings
        for (u32 i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; ++i) {

            // Write the Heightmap (Duplicated per set)
            VkWriteDescriptorSet heightmap_write {};
            heightmap_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            heightmap_write.dstSet = DescriptorSets[i];
            heightmap_write.dstBinding = DescriptorMap::PerFrame::Binding_HeightmapTexture;
            heightmap_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            heightmap_write.descriptorCount = 1;
            heightmap_write.pImageInfo = &heightmap_descriptor_image_info;

            // Write the Chunk SSBO (Unique buffer per set)
            auto chunk_link_buffer_value = Buffers.Get(ChunkDrawListBuffers[i]);
            VkDescriptorBufferInfo chunk_buffer_info {};
            chunk_buffer_info.buffer = chunk_link_buffer_value->Handle;
            chunk_buffer_info.offset = 0;
            chunk_buffer_info.range = chunk_link_buffer_value->Size;

            VkWriteDescriptorSet chunk_ssbo_write {};
            chunk_ssbo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            chunk_ssbo_write.dstSet = DescriptorSets[i];
            chunk_ssbo_write.dstBinding = DescriptorMap::PerFrame::Binding_ChunkDrawListSSBO;
            chunk_ssbo_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            chunk_ssbo_write.descriptorCount = 1;
            chunk_ssbo_write.pBufferInfo = &chunk_buffer_info;

            // Execute writes for Set[i]
            std::array<VkWriteDescriptorSet, 2> writes = { heightmap_write, chunk_ssbo_write };
            vkUpdateDescriptorSets(VkVault::Device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Pipeline Layout creation
    TerrainPipelineLayout = {};

    std::array<VkDescriptorSetLayout, 2> pipeline_layouts = {
        DescriptorManager::GlobalLayout,
        DescriptorManager::PerFrameLayout
    };

    // Single uint32 toggle (terrain.frag's TerrainPushConstants::showDebugColors) - swaps between
    // the procedural texture and the flat per-chunk debug checker at draw time, no pipeline
    // rebuild needed.
    VkPushConstantRange terrain_push_constant_range {};
    terrain_push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    terrain_push_constant_range.offset = 0;
    terrain_push_constant_range.size = sizeof(u32);

    VkPipelineLayoutCreateInfo terrain_pipeline_layout_create_info {};
    terrain_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    terrain_pipeline_layout_create_info.setLayoutCount = static_cast<u32>(pipeline_layouts.size());
    terrain_pipeline_layout_create_info.pSetLayouts = pipeline_layouts.data();
    terrain_pipeline_layout_create_info.pPushConstantRanges = &terrain_push_constant_range;
    terrain_pipeline_layout_create_info.pushConstantRangeCount = 1;

    VK_CHECK(
        vkCreatePipelineLayout(
            VkVault::Device,
            &terrain_pipeline_layout_create_info,
            nullptr,
            &TerrainPipelineLayout
        ),
        "terrain pipeline layout creation failed"
    );

    // Finally creating the terrain VkPipeline itself
    auto dynamic_state_create_info = PipelineDefaults::DefaultPipelineDynamicStateCreateInfo();

    auto rendering_create_info = PipelineDefaults::SceneRenderingCreateInfo();

    auto vertex_input_state = PipelineDefaults::DefaultPipelineVertexInputStateCreateInfo();
    auto input_assembly_state = PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo();
    auto viewport_state = PipelineDefaults::DefaultPipelineViewportStateCreateInfo();
    auto rasterization_state = PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo();
    auto multisample_state = PipelineDefaults::SceneMultisampleStateCreateInfo();
    auto depth_stencil_state = PipelineDefaults::DefaultPipelineDepthStencilStateCreateInfo();
    auto colorblend_state = PipelineDefaults::DefaultPipelineColorBlendStateCreateInfo();

    VkGraphicsPipelineCreateInfo terrain_pipeline_create_info {};
    terrain_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    terrain_pipeline_create_info.pVertexInputState = &vertex_input_state;
    terrain_pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    terrain_pipeline_create_info.pViewportState = &viewport_state;
    terrain_pipeline_create_info.pRasterizationState = &rasterization_state;
    terrain_pipeline_create_info.pMultisampleState = &multisample_state;
    terrain_pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    terrain_pipeline_create_info.pColorBlendState = &colorblend_state;
    terrain_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    terrain_pipeline_create_info.layout = TerrainPipelineLayout;
    terrain_pipeline_create_info.basePipelineIndex = -1;

    terrain_pipeline_create_info.pNext = &rendering_create_info;

    // Add shaders
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    std::vector<u32> shader_buffer;
    shader_buffer.reserve(4096);

    // Vertex shader
    VkPipelineShaderStageCreateInfo vert_shader;
    SpecializationBuilder vert_shader_spec_builder;
    // constant_id 1 (GRID_SCALE) is deliberately unused now - chunk world-size varies per LOD
    // ring, so it moved to a per-instance SSBO field (ChunkInstanceData::Scale) instead.
    vert_shader_spec_builder
        .AddConstant(0, TerrainManager::VerticesPerEdge)
        .AddConstant(2, Config.HeightScale)
        .AddConstant(3, TerrainManager::SeaLevel);

    INC_CHECK(
        CreateShaderStage(
            VK_SHADER_STAGE_VERTEX_BIT,
            "shaders/terrain.vert.spv",
            shader_buffer,
            vert_shader
        ),
        "vertex shader creation failed"
    );
    vert_shader.pSpecializationInfo = vert_shader_spec_builder.Build();

    // Fragment shader
    VkPipelineShaderStageCreateInfo frag_shader;
    SpecializationBuilder frag_shader_spec_builder;
    frag_shader_spec_builder
        .AddConstant(0, static_cast<f32>(TerrainManager::VerticesPerEdge - 1))
        .AddConstant(2, Config.HeightScale)
        .AddConstant(3, TerrainManager::SeaLevel)
        .AddConstant(10, static_cast<f32>(TerrainManager::TotalCoverageRadius));

    INC_CHECK(
        CreateShaderStage(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            "shaders/terrain.frag.spv",
            shader_buffer,
            frag_shader
        ),
        "fragment shader creation failed"
    );

    frag_shader.pSpecializationInfo = frag_shader_spec_builder.Build();

    shader_stages.push_back(vert_shader);
    shader_stages.push_back(frag_shader);

    terrain_pipeline_create_info.stageCount = static_cast<u32>(shader_stages.size());
    terrain_pipeline_create_info.pStages = shader_stages.data();

    VK_CHECK(
        vkCreateGraphicsPipelines(
            VkVault::Device,
            VK_NULL_HANDLE,
            1,
            &terrain_pipeline_create_info,
            nullptr,
            &TerrainPipeline
        ),
        "terrain pipeline creation failed."
    );

    // As of now just destroy the shader modules
    for (auto shader_stage : shader_stages) {
        if (shader_stage.module) { vkDestroyShaderModule(VkVault::Device, shader_stage.module, nullptr); }
    }

    INC_CHECK(PlaneMesh.Upload(), "plane mesh index buffer upload failed");

    return IncResult::SUCCESS;
}

void TerrainPass::Destroy() {
    for (auto& buffer: ChunkDrawListBuffers) {
        Buffers.Del(buffer);
    }

    Buffers.Del(PlaneMesh.Indices);

    if (Heightmap.Sampler) { vkDestroySampler(VkVault::Device, Heightmap.Sampler, nullptr); }
    ImageViews.Del(Heightmap.View);
    Images.Del(Heightmap.Image);

    if (TerrainPipeline) { vkDestroyPipeline(VkVault::Device, TerrainPipeline, nullptr); }
    if (TerrainPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, TerrainPipelineLayout, nullptr); }
}

void TerrainPass::FrameSensibleTransfers() {
    // The drawn set can legitimately be empty now that it's frustum-culled (e.g. looking up from
    // above the terrain), and vkCmdUpdateBuffer rejects a dataSize of 0. Nothing samples the
    // buffer this frame either, since Render() skips the draw at the same count.
    if (TerrainManager::CurrentlyActiveChunks == 0) { return; }

    auto buffer = Buffers.Get(ChunkDrawListBuffers[GRenderer.FrameContext.FrameInFlightIndex]);

    vkCmdUpdateBuffer(
        GRenderer.FrameContext.DrawCommand,
        buffer->Handle,
        0,
        sizeof(TerrainManager::ChunkInstanceData) * TerrainManager::CurrentlyActiveChunks,
        &TerrainManager::ChunkDrawList
    );
}

void TerrainPass::Render() {
    OutTerrainData();

    if (TerrainManager::CurrentlyActiveChunks == 0) { return; }

    VkCommandBuffer& cmd = GRenderer.FrameContext.DrawCommand;

    vkCmdBindIndexBuffer(cmd, Buffers.Get(PlaneMesh.Indices)->Handle, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, TerrainPipeline);

    std::array<VkDescriptorSet, 2> sets_to_bind = {
        GRenderer.GlobalDescriptors.Sets[GRenderer.FrameContext.FrameInFlightIndex], // Set 0
        DescriptorSets[GRenderer.FrameContext.FrameInFlightIndex]                    // Set 1
    };
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        TerrainPipelineLayout,
        DescriptorMap::Global::SetIndex,
        sets_to_bind.size(),
        sets_to_bind.data(),
        0,
        nullptr
    );

    u32 show_debug_colors = ShowChunkDebugColors ? 1u : 0u;
    vkCmdPushConstants(cmd, TerrainPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(u32), &show_debug_colors);

    vkCmdDrawIndexed(cmd, PlaneMeshResource::IndexCount, TerrainManager::CurrentlyActiveChunks, 0, 0, 0);
}

void TerrainPass::PlaneMeshResource::GenerateIndices(u32* indices_begin) {
    u32 terrain_res = TerrainManager::VerticesPerEdge;
    for (u32 z = 0; z < terrain_res - 1; z++) {
        for (u32 x = 0; x < terrain_res - 1; x++) {
            // Calculate the index of the current vertex and neighbors
            u32 top_left = (z * terrain_res) + x;
            u32 top_right = top_left + 1;
            u32 bottom_left = ((z + 1) * terrain_res) + x;
            u32 bottom_right = bottom_left + 1;

            // Triangle 1 (Top-Left -> Bottom-Left -> Top-Right)
            *indices_begin++ = top_left;
            *indices_begin++ = bottom_left;
            *indices_begin++ = top_right;

            // Triangle 2 (Top-Right -> Bottom-Left -> Bottom-Right)
            *indices_begin++ = top_right;
            *indices_begin++ = bottom_left;
            *indices_begin++ = bottom_right;
        }
    }
}

IncResult TerrainPass::PlaneMeshResource::Upload() {
    // Create the actual Plane Mesh index buffer
    Buffer::CreateInfo indices_buffer_create_info = {
        .Size = IndexBufferSize,
        .Type = Buffer::Type::INDEX,
    };
    INC_CHECK(Buffers.Add(indices_buffer_create_info, Indices), "plane mesh index buffer creation failed");

    std::vector<u32> indices_buffer(IndexCount);

    GenerateIndices(indices_buffer.data());

    GTransferPipe.QueueBufferUpload(Indices, 0, indices_buffer.data(), IndexBufferSize);
    GTransferPipe.LazySubmit();

    return IncResult::SUCCESS;
}

void TerrainPass::OutTerrainData() {
    using namespace TerrainManager;

    if (!DebugPanel::BeginSection(DebugPanel::Section::Terrain)) { return; }

    ImGui::Checkbox("Frustum Culling", &CullingEnabled);
    ImGui::Checkbox("Show Chunk Debug Colors", &ShowChunkDebugColors);
    ImGui::Text("Total Drawn: %u / %u", CurrentlyActiveChunks, TotalMaxDrawnChunks);

    // Live generator tuning. Edits only take effect through Regenerate() - automatically when a
    // slider is released (not every drag tick, which would restart the whole world's regeneration
    // continuously), or on demand with the button. Already-streamed terrain keeps drawing until
    // its replacement lands, nearest/visible first.
    if (ImGui::CollapsingHeader("Terrain Generator", ImGuiTreeNodeFlags_DefaultOpen)) {
        static bool auto_regenerate = true;
        bool changed = false;
        auto track = [&changed]() { changed |= ImGui::IsItemDeactivatedAfterEdit(); };

        ImGui::SliderFloat("Peak Sharpness", &Generator.PeakSharpness, 0.0f, 2.5f, "%.2f"); track();
        ImGui::SliderFloat("Mountain Height", &Generator.MountainHeight, 0.1f, 0.85f, "%.2f"); track();
        ImGui::SliderFloat("Mountain Coverage", &Generator.MountainCoverage, -0.5f, 0.5f, "%.2f"); track();
        ImGui::SliderFloat("Mountain Size", &Generator.MountainWavelength, 800.0f, 6000.0f, "%.0f units"); track();
        ImGui::SliderFloat("Erosion", &Generator.ErosionStrength, 0.0f, 10.0f, "%.1f"); track();
        ImGui::SliderFloat("Hills", &Generator.HillAmount, 0.0f, 4.0f, "%.2f"); track();
        ImGui::SliderFloat("Land vs Ocean", &Generator.LandBias, -0.3f, 0.6f, "%.2f"); track();
        ImGui::SliderFloat("Coastline Warp", &Generator.WarpStrength, 0.0f, 2000.0f, "%.0f units"); track();
        ImGui::InputInt("Seed", &Generator.Seed); track();

        ImGui::Checkbox("Regenerate on release", &auto_regenerate);
        ImGui::SameLine();
        bool regenerate = ImGui::Button("Regenerate");
        ImGui::SameLine();
        if (ImGui::Button("Defaults")) {
            Generator = GeneratorParams{};
            regenerate = true;
        }

        if (regenerate || (changed && auto_regenerate)) {
            Regenerate();
        }

        // Stale = still drawing terrain from an older GeneratorEpoch. Only slots PositionToSlot
        // still maps count - an orphaned, already-replaced slot keeps its old Epoch but is never
        // drawn again (see TerrainManager.cpp's finalize step).
        u32 stale = 0;
        auto count_stale = [&stale](const auto& ring) {
            for (u32 i = 0; i < ring.Cache.size(); i++) {
                const CacheSlot& slot = ring.Cache[i];
                if (!slot.Valid || slot.Epoch == GeneratorEpoch) { continue; }
                auto mapped = ring.PositionToSlot.find(PackPosition(slot.Position));
                if (mapped != ring.PositionToSlot.end() && mapped->second == i) { stale++; }
            }
        };
        count_stale(Ring0);
        for (const auto& ring : OuterRings) { count_stale(ring); }

        if (stale > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Regenerating: %u chunks left", stale);
        } else {
            ImGui::TextDisabled("Up to date");
        }
    }

    // Collapsed by default even with the Terrain section open - this ring/chunk breakdown is the
    // single most space-consuming panel of all of them.
    if (ImGui::CollapsingHeader("Terrain Draw Data")) {
        // Per-ring breakdown confirms outer rings stay bounded in chunk count even as effective
        // draw distance grows, instead of just one opaque global total.
        if (ImGui::BeginTable("TerrainRings", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Ring");
            ImGui::TableSetupColumn("ChunkScale");
            ImGui::TableSetupColumn("Drawn");
            ImGui::TableSetupColumn("Culled");
            ImGui::TableSetupColumn("Dark");
            ImGui::TableSetupColumn("Cached");
            ImGui::TableHeadersRow();

            // "Dark" = VisibleMissingLastFrame: in the frustum right now but not yet generated -
            // measured directly, not inferred from Drawn/Culled/Cached.
            auto ring_row = [](const char* name, f64 scale, u32 drawn, u32 max_drawn, u32 culled, u32 dark, u32 cached, u32 max_cached) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%s", name);
                ImGui::TableNextColumn(); ImGui::Text("%.0f", scale);
                ImGui::TableNextColumn(); ImGui::Text("%u / %u", drawn, max_drawn);
                ImGui::TableNextColumn(); ImGui::Text("%u", culled);
                if (dark > 0) {
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%u", dark);
                } else {
                    ImGui::TableNextColumn(); ImGui::Text("0");
                }
                ImGui::TableNextColumn(); ImGui::Text("%u / %u", cached, max_cached);
            };

            u32 ring0_cached = 0;
            for (u32 i = 0; i < MaxCachedChunks; i++) { if (Ring0.Cache[i].Valid) { ring0_cached++; } }
            ring_row("Ring 0", Ring0.ChunkScale, Ring0.DebugStats.DrawnLastFrame, MaxDrawnChunks,
                     Ring0.DebugStats.CulledLastFrame, Ring0.DebugStats.VisibleMissingLastFrame, ring0_cached, MaxCachedChunks);

            const char* outer_ring_names[OuterRingCount] = { "Ring 1", "Ring 2", "Ring 3", "Ring 4" };
            for (u32 i = 0; i < OuterRingCount; i++) {
                u32 cached = 0;
                for (u32 j = 0; j < OuterRingMaxCachedChunks; j++) { if (OuterRings[i].Cache[j].Valid) { cached++; } }
                ring_row(outer_ring_names[i], OuterRings[i].ChunkScale, OuterRings[i].DebugStats.DrawnLastFrame,
                         OuterRingMaxDrawnChunks, OuterRings[i].DebugStats.CulledLastFrame,
                         OuterRings[i].DebugStats.VisibleMissingLastFrame, cached, OuterRingMaxCachedChunks);
            }

            ImGui::EndTable();
        }

        if (ImGui::TreeNode("Ring 0 Streaming")) {
            if (Ring0.DebugStats.GenerationsInFlight > 0) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                    "Generating: %u / %u pool slots busy",
                    Ring0.DebugStats.GenerationsInFlight, Ring0GenerationPoolSize
                );
            } else {
                ImGui::TextDisabled("Idle");
            }

            ImGui::Text("Generated:  %llu", (unsigned long long)Ring0.DebugStats.ChunksGenerated);
            ImGui::Text("Started:    %llu", (unsigned long long)Ring0.DebugStats.GenerationsStarted);
            ImGui::Text("Evictions:  %llu", (unsigned long long)Ring0.DebugStats.Evictions);
            ImGui::Text("Last gen:   %.2f ms", static_cast<f64>(Ring0.DebugStats.LastGenerationMs));

            ImGui::TreePop();
        }

        // Per-chunk detail (Cache Slot / Heightmap Preview / Eviction Log, previously here for
        // every chunk) is dropped for now in favor of the per-ring summary above, which is what
        // actually validates the LOD design end to end - worth reintroducing scoped to one
        // selected ring if per-chunk debugging is needed again later.
    }

    DebugPanel::EndSection();
}
