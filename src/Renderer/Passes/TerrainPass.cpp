#include "TerrainPass.hpp"

#include <glm/ext.hpp>

#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Resources/TransferPipe.hpp"
#include "Renderer/Resources/ResourceManager.hpp"
#include "Engine/TerrainManager/TerrainDefinitions.hpp"

namespace TerrainPass {
    // Push constants
    struct TerrainPushConstants {
        glm::mat4 CameraMVP;
        glm::vec4 PlayerPosition;
    };

    TerrainPushConstants TerrainPushConstants {};

    namespace Descriptor {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
    };

    namespace PlaneMesh {
        Buffer::Id Indices;

        // Easier access for vkBindIndexBuffer()
        VkBuffer VkBuffer; /////// REMOVE THIS ASP

        void GenerateIndices(u32* IndicesBegin);
        void Upload();
    }

    namespace Heightmap {
        Image::Id Image;
        ImageView::Id ImageView;
        VkSampler Sampler;
    }

    namespace ChunkLinks {
        Buffer::Id Data;
    }

    // Terrain pipeline
    VkPipeline TerrainPipeline {};
    VkPipelineLayout TerrainPipelineLayout {};

    IncResult Create() {
        {
            VkShaderStageFlags all_shader_stages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

            // Terrain heightmap
            {
                Image::CreateInfo heightmap_image_create_desc;
                heightmap_image_create_desc.Width = TerrainConfig::Mesh::VerticesPerEdge;
                heightmap_image_create_desc.Height = TerrainConfig::Mesh::VerticesPerEdge;
                heightmap_image_create_desc.ArrayLayers = TerrainConfig::Streaming::MaxActiveChunks;
                heightmap_image_create_desc.Format = TerrainConfig::Memory::HeightmapFormat;
                heightmap_image_create_desc.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

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

            // Chunk to Heightmap linking
            {
                Buffer::CreateInfo chunk_instance_buffer_create_info = {
                    .Size = TerrainConfig::Streaming::MaxActiveChunks * sizeof(TerrainConfig::Memory::ChunkInstanceData),
                    .Type = Buffer::Type::SSBO,
                };

                ChunkLinks::Data = Buffer::Add(chunk_instance_buffer_create_info);
            }

            // Terrain System Descriptors
            {
                // Heightmap Texture Sampler descriptor
                auto heightmap_image_view_value = ImageView::Get(Heightmap::ImageView);
                VkDescriptorImageInfo heightmap_descriptor_image_info {};
                heightmap_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                heightmap_descriptor_image_info.imageView = heightmap_image_view_value->ImageView;
                heightmap_descriptor_image_info.sampler = Heightmap::Sampler;

                VkDescriptorSetLayoutBinding heightmap_set_layout_binding {};
                heightmap_set_layout_binding.binding = 0;
                heightmap_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                heightmap_set_layout_binding.descriptorCount = 1;
                heightmap_set_layout_binding.stageFlags = all_shader_stages;
                heightmap_set_layout_binding.pImmutableSamplers = nullptr;

                VkWriteDescriptorSet heightmap_sampler_write {};
                heightmap_sampler_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                heightmap_sampler_write.dstBinding = 0;
                heightmap_sampler_write.dstArrayElement = 0;
                heightmap_sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                heightmap_sampler_write.descriptorCount = 1;
                heightmap_sampler_write.pImageInfo = &heightmap_descriptor_image_info;

                // Chunk to Heightmap descriptor
                // TODO: The fact I'm not carrying offsets arround is most definitvelly a bad signal
                auto chunk_link_buffer_value = Buffer::Get(ChunkLinks::Data);
                VkDescriptorBufferInfo chunk_instance_descriptor_buffer_info {};
                chunk_instance_descriptor_buffer_info.buffer = chunk_link_buffer_value->Buffer;
                chunk_instance_descriptor_buffer_info.offset = 0;
                chunk_instance_descriptor_buffer_info.range = chunk_link_buffer_value->Size;

                VkDescriptorSetLayoutBinding chunk_instance_renderer_binding {};
                chunk_instance_renderer_binding.binding = 1;
                chunk_instance_renderer_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                chunk_instance_renderer_binding.descriptorCount = 1;
                chunk_instance_renderer_binding.stageFlags = all_shader_stages;
                chunk_instance_renderer_binding.pImmutableSamplers = nullptr;

                VkWriteDescriptorSet chunk_link_ssbo_write {};
                chunk_link_ssbo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                chunk_link_ssbo_write.dstBinding = 1;
                chunk_link_ssbo_write.dstArrayElement = 0;
                chunk_link_ssbo_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                chunk_link_ssbo_write.descriptorCount = 1;
                chunk_link_ssbo_write.pBufferInfo = &chunk_instance_descriptor_buffer_info;

                // Descriptor layout
                std::array<VkDescriptorSetLayoutBinding, 2> layout_bindings = {
                    heightmap_set_layout_binding,
                    chunk_instance_renderer_binding
                };
                VkDescriptorSetLayoutCreateInfo descriptor_layout_create_info {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .bindingCount = static_cast<u32>(layout_bindings.size()),
                    .pBindings = layout_bindings.data()
                };

                VK_CHECK(
                    vkCreateDescriptorSetLayout(
                        VkVault::Device,
                        &descriptor_layout_create_info,
                        nullptr,
                        &Descriptor::layout
                    ),
                    "terrain descriptor set layout creation failed"
                );

                // Descriptor pool
                VkDescriptorPoolSize heightmap_sampler_pool_size = {
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1
                };
                VkDescriptorPoolSize heightmap_ssbo_pool_size = {
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1
                };
                std::array<VkDescriptorPoolSize, 2> pool_sizes = {
                    heightmap_sampler_pool_size,
                    heightmap_ssbo_pool_size
                };

                VkDescriptorPoolCreateInfo descriptor_pool_info {};
                descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                descriptor_pool_info.poolSizeCount = static_cast<u32>(pool_sizes.size());
                descriptor_pool_info.pPoolSizes = pool_sizes.data();
                descriptor_pool_info.maxSets = 1;

                VK_CHECK(
                    vkCreateDescriptorPool(VkVault::Device, &descriptor_pool_info, nullptr, &Descriptor::pool),
                    "descriptor pool creation failed"
                );

                VkDescriptorSetAllocateInfo descriptor_alloc_info {};
                descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                descriptor_alloc_info.descriptorPool = Descriptor::pool;
                descriptor_alloc_info.descriptorSetCount = 1;
                descriptor_alloc_info.pSetLayouts = &Descriptor::layout;

                VK_CHECK(
                    vkAllocateDescriptorSets(VkVault::Device, &descriptor_alloc_info, &Descriptor::set),
                    "descriptor set allocation failed"
                );

                std::array<VkWriteDescriptorSet, 2> terrain_pass_writes = {
                    heightmap_sampler_write, chunk_link_ssbo_write
                };

                for (VkWriteDescriptorSet& write : terrain_pass_writes) {
                    write.dstSet = Descriptor::set;
                }

                vkUpdateDescriptorSets(VkVault::Device, static_cast<u32>(terrain_pass_writes.size()), terrain_pass_writes.data(), 0, nullptr);
            }

            TerrainPipelineLayout = {};
            VkPushConstantRange terrain_push_constant_ranges = {
                .stageFlags = all_shader_stages,
                .offset = 0,
                .size = static_cast<u32>(sizeof(TerrainPushConstants))
            };

            VkPipelineLayoutCreateInfo terrain_pipeline_layout_create_info {};
            terrain_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            terrain_pipeline_layout_create_info.setLayoutCount = 1;
            terrain_pipeline_layout_create_info.pSetLayouts = &Descriptor::layout;
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
            std::vector<VkDynamicState> dynamic_states {};
            dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            VkPipelineDynamicStateCreateInfo dynamic_state_create_info {};
            dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state_create_info.dynamicStateCount = static_cast<u32>(dynamic_states.size());
            dynamic_state_create_info.pDynamicStates = dynamic_states.data();

            VkPipelineRenderingCreateInfo rendering_create_info {};
            rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_create_info.colorAttachmentCount = static_cast<u32>(VkVault::ColorAttachmentFormats.size());
            rendering_create_info.pColorAttachmentFormats = VkVault::ColorAttachmentFormats.data();
            rendering_create_info.depthAttachmentFormat = RendererConfig::DepthBuffer::Format;
            rendering_create_info.stencilAttachmentFormat = RendererConfig::DepthBuffer::Format;

            VkGraphicsPipelineCreateInfo terrain_pipeline_create_info {};
            terrain_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            terrain_pipeline_create_info.pVertexInputState = &PipelineDefaults::DefaultPipelineVertexInputStateCreateInfo;
            terrain_pipeline_create_info.pInputAssemblyState = &PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo;
            terrain_pipeline_create_info.pViewportState = &PipelineDefaults::DefaultPipelineViewportStateCreateInfo;
            terrain_pipeline_create_info.pRasterizationState = &PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo;
            terrain_pipeline_create_info.pMultisampleState = &PipelineDefaults::DefaultPipelineMultisampleStateCreateInfo;
            terrain_pipeline_create_info.pDepthStencilState = &PipelineDefaults::DefaultPipelineDepthStencilStateCreateInfo;
            terrain_pipeline_create_info.pColorBlendState = &PipelineDefaults::DefaultPipelineColorBlendStateCreateInfo;
            terrain_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
            terrain_pipeline_create_info.layout = TerrainPipelineLayout;
            terrain_pipeline_create_info.basePipelineIndex = -1;
            terrain_pipeline_create_info.pNext = &rendering_create_info;

            // Add shaders
            std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
            std::vector<u32> shader_buffer;
            shader_buffer.reserve(4096);

            shader_stages.push_back(
                CreateShaderStage(
                    VK_SHADER_STAGE_VERTEX_BIT,
                    "shaders/terrain.vert.spv",
                    shader_buffer
                )
            );
            shader_stages.push_back(
                CreateShaderStage(
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    "shaders/terrain.frag.spv",
                    shader_buffer
                )
            );

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
        }

        PlaneMesh::Upload();

        // Zeroing terrain push constants
        TerrainPushConstants = {
            .CameraMVP = glm::mat4(0),
            .PlayerPosition = glm::vec4(0)
        };

        return IncResult::SUCCESS;
    }

    void Destroy() {
        Buffer::Del(ChunkLinks::Data);

        Buffer::Del(PlaneMesh::Indices);

        if (Heightmap::Sampler) { vkDestroySampler(VkVault::Device, Heightmap::Sampler, nullptr); }
        Image::Del(Heightmap::Image);

        if (Descriptor::pool) { vkDestroyDescriptorPool(VkVault::Device, Descriptor::pool, nullptr); }
        if (Descriptor::layout) { vkDestroyDescriptorSetLayout(VkVault::Device, Descriptor::layout, nullptr); }

        if (TerrainPipeline) { vkDestroyPipeline(VkVault::Device, TerrainPipeline, nullptr); }
        if (TerrainPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, TerrainPipelineLayout, nullptr); }
    }

    void Render(VkCommandBuffer cmd) {
        vkCmdBindIndexBuffer(cmd, PlaneMesh::VkBuffer, 0, VK_INDEX_TYPE_UINT32);

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
            0, // Probably a bad idea the way I carry this binding value lol TEXTURE_SAMPLER_BINDING,
            1,
            &Descriptor::set,
            0,
            nullptr
        );

        vkCmdDrawIndexed(cmd, TerrainConfig::Mesh::IndexCount, TerrainConfig::Streaming::MaxActiveChunks, 0, 0, 0);
    }

    namespace PlaneMesh {
        void GenerateIndices(u32* IndicesBegin) {
            u32 TerrainRes = TerrainConfig::Mesh::VerticesPerEdge;
            for (u32 z = 0; z < TerrainRes - 1; z++) {
                for (u32 x = 0; x < TerrainRes - 1; x++) {
                    // Calculate the index of the current vertex and neighbors
                    u32 topLeft = (z * TerrainRes) + x;
                    u32 topRight = topLeft + 1;
                    u32 bottomLeft = ((z + 1) * TerrainRes) + x;
                    u32 bottomRight = bottomLeft + 1;

                    // Triangle 1 (Top-Left -> Bottom-Left -> Top-Right)
                    *IndicesBegin++ = topLeft;
                    *IndicesBegin++ = bottomLeft;
                    *IndicesBegin++ = topRight;

                    // Triangle 2 (Top-Right -> Bottom-Left -> Bottom-Right)
                    *IndicesBegin++ = topRight;
                    *IndicesBegin++ = bottomLeft;
                    *IndicesBegin++ = bottomRight;
                }
            }
        }

        void Upload() {
            // Create the actual Plane Mesh index buffer
            Buffer::CreateInfo IndiceCreateInfo = {
                .Size = TerrainConfig::Mesh::IndexBufferSize,
                .Type = Buffer::Type::INDEX,
            };
            Indices = Buffer::Add(IndiceCreateInfo);
            VkBuffer = Buffer::Get(PlaneMesh::Indices)->Buffer;

            std::array<u32, TerrainConfig::Mesh::IndexBufferSize> indices_buffer;
            GenerateIndices(indices_buffer.data());

            TransferPipe::Ticket indices_upload = TransferPipe::QueueBufferUpload(Indices, 0, indices_buffer.data(), TerrainConfig::Mesh::IndexBufferSize);
            TransferPipe::FullSubmit();
            TransferPipe::WaitOn(indices_upload);
        }
    }
}
