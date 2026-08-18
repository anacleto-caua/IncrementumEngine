#include "TerrainPass.hpp"

#include <array>

#include <imgui.h>
#include <glm/ext.hpp>

#include "Renderer/VkVault.hpp"
#include "Renderer/Renderer_Internal.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Resources/TransferPipe.hpp"
#include "Renderer/Resources/ResourceManager.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"
#include "Renderer/Vk/ShaderSpecializationBuilder.hpp"

namespace TerrainPass {
    namespace Descriptor {
        std::array<VkDescriptorSet, Renderer::MAX_FRAMES_IN_FLIGHT> Sets = { VK_NULL_HANDLE };
    };

    namespace PlaneMesh {
        Buffer::Id Indices;

        // Only PlaneMesh (and the draw call in Render()) care about the index buffer's shape -
        // nothing outside this file references these.
        constexpr u32 IndexCount = (TerrainManager::VerticesPerEdge - 1) * (TerrainManager::VerticesPerEdge - 1) * 6;
        constexpr u32 IndexBufferSize = IndexCount * sizeof(u32);

        void GenerateIndices(u32* IndicesBegin);
        IncResult Upload();
    }

    namespace Heightmap {
        // Image::Id Image; is declared in TerrainPass.hpp - TerrainManager needs it to queue chunk uploads
        ImageView::Id ImageView;
        VkSampler Sampler;
    }

    namespace DebugUI {
        void OutTerrainData();
    }

    std::array<Buffer::Id, Renderer::MAX_FRAMES_IN_FLIGHT> ChunkDrawListBuffers;

    // Terrain pipeline
    VkPipeline TerrainPipeline {};
    VkPipelineLayout TerrainPipelineLayout {};

    IncResult Create() {
        // Image for terrain heightmap
        {
            Image::CreateInfo heightmap_image_create_desc;
            heightmap_image_create_desc.Width = TerrainManager::VerticesPerEdge;
            heightmap_image_create_desc.Height = TerrainManager::VerticesPerEdge;
            heightmap_image_create_desc.ArrayLayers = TerrainManager::MaxCachedChunks;
            heightmap_image_create_desc.Format = Heightmap::Format;
            heightmap_image_create_desc.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            heightmap_image_create_desc.OwnerQueue = QueueRole::Graphics;
            heightmap_image_create_desc.UsageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            INC_CHECK(Image::Add(heightmap_image_create_desc, Heightmap::Image), "heightmap image creation failed");
            Image::Value* heightmap_image_value = Image::Get(Heightmap::Image);
            auto heightmap_image_view_create_info = ImageView::FillCreateInfo(heightmap_image_value);
            INC_CHECK(ImageView::Add(heightmap_image_view_create_info, Heightmap::ImageView), "heightmap image view creation failed");

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
                vkCreateSampler(VkVault::Device, &heightmap_sampler_info, nullptr, &Heightmap::Sampler),
                "heightmap sampler creation failed"
            );
        }

        // Buffer for chunk draw list
        {
            Buffer::CreateInfo chunk_instance_buffer_create_info = {
                .Size = TerrainManager::MaxDrawnChunks * sizeof(TerrainManager::ChunkInstanceData),
                .Type = Buffer::Type::SSBO,
            };

            for (auto& draw_list_buffer : ChunkDrawListBuffers) {
                INC_CHECK(Buffer::Add(chunk_instance_buffer_create_info, draw_list_buffer), "chunk draw list buffer creation failed");
            }
        }

        // Descriptors
        {
            // Allocate the array of Terrain Sets (Set 1)
            DescriptorManager::AllocateSets(
                DescriptorManager::PerFrameLayout,
                Renderer::MAX_FRAMES_IN_FLIGHT,
                Descriptor::Sets.data()
            );

            auto heightmap_image_view_value = ImageView::Get(Heightmap::ImageView);
            VkDescriptorImageInfo heightmap_descriptor_image_info {};
            heightmap_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            heightmap_descriptor_image_info.imageView = heightmap_image_view_value->ImageView;
            heightmap_descriptor_image_info.sampler = Heightmap::Sampler;

            // Loop through each frame in flight and write both bindings
            for (u32 i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; ++i) {

                // Write the Heightmap (Duplicated per set)
                VkWriteDescriptorSet heightmap_write {};
                heightmap_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                heightmap_write.dstSet = Descriptor::Sets[i];
                heightmap_write.dstBinding = DescriptorMap::PerFrame::Binding_HeightmapTexture;
                heightmap_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                heightmap_write.descriptorCount = 1;
                heightmap_write.pImageInfo = &heightmap_descriptor_image_info;

                // Write the Chunk SSBO (Unique buffer per set)
                auto chunk_link_buffer_value = Buffer::Get(ChunkDrawListBuffers[i]);
                VkDescriptorBufferInfo chunk_buffer_info {};
                chunk_buffer_info.buffer = chunk_link_buffer_value->Buffer;
                chunk_buffer_info.offset = 0;
                chunk_buffer_info.range = chunk_link_buffer_value->Size;

                VkWriteDescriptorSet chunk_ssbo_write {};
                chunk_ssbo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                chunk_ssbo_write.dstSet = Descriptor::Sets[i];
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

        VkPipelineLayoutCreateInfo terrain_pipeline_layout_create_info {};
        terrain_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        terrain_pipeline_layout_create_info.setLayoutCount = static_cast<u32>(pipeline_layouts.size());
        terrain_pipeline_layout_create_info.pSetLayouts = pipeline_layouts.data();
        terrain_pipeline_layout_create_info.pPushConstantRanges = nullptr;
        terrain_pipeline_layout_create_info.pushConstantRangeCount = 0;

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
        std::array<VkDynamicState, 2> dynamic_states = {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}};

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
        rendering_create_info.depthAttachmentFormat = Renderer::DepthBuffer::Format;
        rendering_create_info.stencilAttachmentFormat = Renderer::DepthBuffer::Format;

        auto vertex_input_state = PipelineDefaults::DefaultPipelineVertexInputStateCreateInfo();
        auto input_assembly_state = PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo();
        auto viewport_state = PipelineDefaults::DefaultPipelineViewportStateCreateInfo();
        auto rasterization_state = PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo();
        auto multisample_state = PipelineDefaults::DefaultPipelineMultisampleStateCreateInfo();
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
        vert_shader_spec_builder
            .AddConstant(0, TerrainManager::VerticesPerEdge)
            .AddConstant(1, static_cast<f32>(TerrainManager::ChunkScale))
            .AddConstant(2, Config.HeightScale);

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
            .AddConstant(0, static_cast<f32>(TerrainManager::VerticesPerEdge - 1));

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

        INC_CHECK(PlaneMesh::Upload(), "plane mesh index buffer upload failed");

        return IncResult::SUCCESS;
    }

    void Destroy() {
        for (auto& buffer: ChunkDrawListBuffers) {
            Buffer::Del(buffer);
        }

        Buffer::Del(PlaneMesh::Indices);

        if (Heightmap::Sampler) { vkDestroySampler(VkVault::Device, Heightmap::Sampler, nullptr); }
        Image::Del(Heightmap::Image);

        if (TerrainPipeline) { vkDestroyPipeline(VkVault::Device, TerrainPipeline, nullptr); }
        if (TerrainPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, TerrainPipelineLayout, nullptr); }
    }

    void FrameSensibleTransfers() {
        auto buffer = Buffer::Get(ChunkDrawListBuffers[Renderer::FrameContext.FrameInFlightIndex]);

        vkCmdUpdateBuffer(
            Renderer::FrameContext.DrawCommand,
            buffer->Buffer,
            0,
            sizeof(TerrainManager::ChunkInstanceData) * TerrainManager::CurrentllyActiveChunks,
            &TerrainManager::ChunkDrawList
        );
    }

    void Render() {
        DebugUI::OutTerrainData();

        VkCommandBuffer& cmd = Renderer::FrameContext.DrawCommand;

        vkCmdBindIndexBuffer(cmd, Buffer::Get(PlaneMesh::Indices)->Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, TerrainPipeline);

        std::array<VkDescriptorSet, 2> sets_to_bind = {
            Renderer::GlobalDescriptors::Sets[Renderer::FrameContext.FrameInFlightIndex], // Set 0
            Descriptor::Sets[Renderer::FrameContext.FrameInFlightIndex]                   // Set 1
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

        vkCmdDrawIndexed(cmd, PlaneMesh::IndexCount, TerrainManager::CurrentllyActiveChunks, 0, 0, 0);
    }

    namespace PlaneMesh {
        void GenerateIndices(u32* indices_begin) {
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

        IncResult Upload() {
            // Create the actual Plane Mesh index buffer
            Buffer::CreateInfo indices_buffer_create_info = {
                .Size = IndexBufferSize,
                .Type = Buffer::Type::INDEX,
            };
            INC_CHECK(Buffer::Add(indices_buffer_create_info, Indices), "plane mesh index buffer creation failed");

            std::vector<u32> indices_buffer(IndexCount);

            GenerateIndices(indices_buffer.data());

            TransferPipe::QueueBufferUpload(Indices, 0, indices_buffer.data(), IndexBufferSize);
            TransferPipe::LazySubmit();

            return IncResult::SUCCESS;
        }
    }

    namespace DebugUI {
        void OutTerrainData() {
            using namespace TerrainManager;

            if (ImGui::CollapsingHeader("Terrain Draw Data", ImGuiTreeNodeFlags_DefaultOpen)) {
                u32 cached_count = 0;
                for (u32 i = 0; i < MaxCachedChunks; i++) {
                    if (Cache[i].Valid) { cached_count++; }
                }

                ImGui::Text("Drawn:  %u / %u", CurrentllyActiveChunks, MaxDrawnChunks);
                ImGui::Text("Cached: %u / %u", cached_count, MaxCachedChunks);
                ImGui::Separator();

                // Iterate only up to the currently active chunks to save UI performance
                for (u32 i = 0; i < CurrentllyActiveChunks; ++i)
                {
                    const auto& draw_data = ChunkDrawList[i];
                    u32 cache_index = draw_data.TextureLayer;

                    // Create a unique tree node for each chunk using its index as the ID
                    if (ImGui::TreeNode((void*)(intptr_t)i, "Chunk [%u]", i))
                    {
                        // Draw Data
                        if (ImGui::TreeNode("Instance Draw Data"))
                        {
                            ImGui::Text("World Pos:     (%d, %d)", draw_data.WorldPos.x, draw_data.WorldPos.y);
                            ImGui::Text("Texture Layer: %u", draw_data.TextureLayer);
                            ImGui::TextDisabled("Padding:       %u", draw_data.padding); // Disabled text color for padding
                            ImGui::TreePop();
                        }

                        // Heightmap Streaming Status
                        if (ImGui::TreeNode("Cache Slot")) {

                            const auto& slot = Cache[cache_index];

                            ImGui::Text("Position: (%d, %d)", slot.Position.x, slot.Position.y);
                            ImGui::Text("Last Used Tick: %llu", (unsigned long long)slot.LastUsedTick);

                            if (slot.Valid) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Valid: True");
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Valid: False");
                            }
                            ImGui::TreePop();
                        }

                        // Heightmap Data Preview
                        if (ImGui::TreeNode("Heightmap Preview")) {
                            const u32 edge = VerticesPerEdge;
                            ImGui::Text("Grid Size: %u x %u", edge, edge);

                            // Show a quick sample of the corners to verify data is loaded
                            ImGui::BulletText("Top-Left [0][0]:     %u", HeightmapData[cache_index][0][0]);
                            ImGui::BulletText("Top-Right [0][N]:    %u", HeightmapData[cache_index][0][edge - 1]);
                            ImGui::BulletText("Bot-Left [N][0]:     %u", HeightmapData[cache_index][edge - 1][0]);
                            ImGui::BulletText("Bot-Right [N][N]:    %u", HeightmapData[cache_index][edge - 1][edge - 1]);

                            ImGui::TreePop();
                        }

                        ImGui::TreePop(); // End Chunk [i]
                    }
                }
            }
        }
    }
}
