#include "TerrainPass.hpp"

#include <array>

#include <glm/ext.hpp>

#include "Renderer/VkVault.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Resources/TransferPipe.hpp"
#include "Renderer/Resources/ResourceManager.hpp"
#include "Engine/TerrainManager/TerrainManager.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"
#include "Engine/TerrainManager/TerrainDefinitions.hpp"

namespace TerrainPass {
    // Push constants
    struct TerrainPushConstants {
        glm::mat4 CameraMVP;
        glm::vec3 PlayerPosition;
        f32 padding;
    };

    TerrainPushConstants TerrainPushConstants {};

    namespace Descriptor {
        std::array<VkDescriptorSet, RendererConfig::MAX_FRAMES_IN_FLIGHT> Sets = { VK_NULL_HANDLE };
    };

    namespace PlaneMesh {
        Buffer::Id Indices;

        void GenerateIndices(u32* IndicesBegin);
        void Upload();
    }

    namespace Heightmap {
        Image::Id Image;
        ImageView::Id ImageView;
        VkSampler Sampler;
    }

    std::array<Buffer::Id, RendererConfig::MAX_FRAMES_IN_FLIGHT> ChunkDrawListBuffers;

    // Terrain pipeline
    VkPipeline TerrainPipeline {};
    VkPipelineLayout TerrainPipelineLayout {};

    IncResult Create() {
        VkShaderStageFlags all_shader_stages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

        // Image for terrain heightmap
        {
            Image::CreateInfo heightmap_image_create_desc;
            heightmap_image_create_desc.Width = TerrainConfig::Mesh::VerticesPerEdge;
            heightmap_image_create_desc.Height = TerrainConfig::Mesh::VerticesPerEdge;
            heightmap_image_create_desc.ArrayLayers = TerrainConfig::Streaming::MaxActiveChunks;
            heightmap_image_create_desc.Format = TerrainConfig::Memory::HeightmapFormat;
            heightmap_image_create_desc.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            heightmap_image_create_desc.OwnerQueue = &VkVault::Graphics;

            Heightmap::Image = Image::Add(heightmap_image_create_desc);
            Image::Value* heightmap_image_value = Image::Get(Heightmap::Image);
            auto heightmap_image_view_create_info = ImageView::FillCreateInfo(heightmap_image_value);
            Heightmap::ImageView = ImageView::Add(heightmap_image_view_create_info);

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
                .Size = TerrainConfig::Streaming::MaxActiveChunks * sizeof(TerrainConfig::Memory::ChunkInstanceData),
                .Type = Buffer::Type::SSBO,
            };

            for (auto& draw_list_buffer : ChunkDrawListBuffers) {
                draw_list_buffer = Buffer::Add(chunk_instance_buffer_create_info);
            }
        }

        // Descriptors
        {
            // Allocate the array of Terrain Sets (Set 1)
            DescriptorManager::AllocateSets(
                DescriptorManager::PerFrameLayout,
                RendererConfig::MAX_FRAMES_IN_FLIGHT,
                Descriptor::Sets.data()
            );

            auto heightmap_image_view_value = ImageView::Get(Heightmap::ImageView);
            VkDescriptorImageInfo heightmap_descriptor_image_info {};
            heightmap_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            heightmap_descriptor_image_info.imageView = heightmap_image_view_value->ImageView;
            heightmap_descriptor_image_info.sampler = Heightmap::Sampler;

            // Loop through each frame in flight and write both bindings
            for (u32 i = 0; i < RendererConfig::MAX_FRAMES_IN_FLIGHT; ++i) {

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
        VkPushConstantRange terrain_push_constant_ranges = {
            .stageFlags = all_shader_stages,
            .offset = 0,
            .size = static_cast<u32>(sizeof(TerrainPushConstants))
        };

        std::array<VkDescriptorSetLayout, 2> pipeline_layouts = {
            DescriptorManager::GlobalLayout,
            DescriptorManager::PerFrameLayout
        };

        VkPipelineLayoutCreateInfo terrain_pipeline_layout_create_info {};
        terrain_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        terrain_pipeline_layout_create_info.setLayoutCount = static_cast<u32>(pipeline_layouts.size());
        terrain_pipeline_layout_create_info.pSetLayouts = pipeline_layouts.data();
        terrain_pipeline_layout_create_info.pPushConstantRanges = &terrain_push_constant_ranges;
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
        rendering_create_info.depthAttachmentFormat = RendererConfig::DepthBuffer::Format;
        rendering_create_info.stencilAttachmentFormat = RendererConfig::DepthBuffer::Format;

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

        VkPipelineShaderStageCreateInfo vert_shader;
        INC_CHECK(
            CreateShaderStage(
                VK_SHADER_STAGE_VERTEX_BIT,
                "shaders/terrain.vert.spv",
                shader_buffer,
                vert_shader
            ),
            "vertex shader creation failed"
        );

        VkPipelineShaderStageCreateInfo frag_shader;
        INC_CHECK(
            CreateShaderStage(
                VK_SHADER_STAGE_FRAGMENT_BIT,
                "shaders/terrain.frag.spv",
                shader_buffer,
                frag_shader
            ),
            "fragment shader creation failed"
        );

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

        PlaneMesh::Upload();
        // Write the first valid data
        TerrainManager::Init();

        for (auto& buffer : ChunkDrawListBuffers) {
            // TODO: Move this to the frame structure
                TransferPipe::QueueBufferUpload(
                    buffer,
                    0,
                    TerrainManager::ChunkDrawList.data(),
                    TerrainManager::ChunkDrawList.size()
                );
        }

        for (u32 i = 0; i < TerrainManager::HeightmapData.size(); i++) {
                TransferPipe::QueueImageSliceUpload(
                    Heightmap::Image,
                    i,
                    &TerrainManager::HeightmapData[i],
                    sizeof(TerrainManager::Heightmap)
                );
        }

        TransferPipe::LazySubmit();

        analog::error("CODE WILL BREAK, IT'S BROKEN BELLOW THIS POINT");
        return IncResult::FAIL;

        // Zeroing terrain push constants
        TerrainPushConstants = {
            .CameraMVP = glm::mat4(0),
            .PlayerPosition = glm::vec3(0),
            .padding = .0
        };

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

    void Render() {
        VkCommandBuffer& cmd = Renderer::FrameContext.DrawCommand;

        vkCmdBindIndexBuffer(cmd, Buffer::Get(PlaneMesh::Indices)->Buffer, 0, VK_INDEX_TYPE_UINT32);

        TerrainPushConstants.CameraMVP = Renderer::CurrentCamera->ModelViewProjection;
        TerrainPushConstants.PlayerPosition = Renderer::CurrentCamera->Position;
        vkCmdPushConstants(
            cmd,
            TerrainPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TerrainPushConstants),
            &TerrainPushConstants
        );

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, TerrainPipeline);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            TerrainPipelineLayout,
            DescriptorMap::PerFrame::SetIndex,
            1,
            &Descriptor::Sets[Renderer::FrameContext.FrameInFlightIndex],
            0,
            nullptr
        );

        vkCmdDrawIndexed(cmd, TerrainConfig::Mesh::IndexCount, TerrainManager::CurrentllyActiveChunks, 0, 0, 0);
    }

    namespace PlaneMesh {
        void GenerateIndices(u32* indices_begin) {
            u32 terrain_res = TerrainConfig::Mesh::VerticesPerEdge;
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

        void Upload() {
            // Create the actual Plane Mesh index buffer
            Buffer::CreateInfo indices_buffer_create_info = {
                .Size = TerrainConfig::Mesh::IndexBufferSize,
                .Type = Buffer::Type::INDEX,
            };
            Indices = Buffer::Add(indices_buffer_create_info);

            std::vector<u32> indices_buffer(TerrainConfig::Mesh::IndexCount);

            TransferPipe::QueueBufferUpload(Indices, 0, indices_buffer.data(), TerrainConfig::Mesh::IndexBufferSize);
            TransferPipe::LazySubmit();
        }
    }
}
