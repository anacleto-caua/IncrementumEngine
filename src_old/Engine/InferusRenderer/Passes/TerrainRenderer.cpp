#include "TerrainRenderer.hpp"

#include <spdlog/spdlog.h>

#include "Engine/InferusRenderer/Recipes.hpp"
#include "Engine/Systems/Terrain/TerrainConfig.hpp"
#include "Engine/InferusRenderer/VulkanContext.hpp"
#include "Engine/Systems/Terrain/TerrainSystem.hpp"
#include "Engine/InferusRenderer/RendererConfig.hpp"
#include "Engine/InferusRenderer/Image/ImageSystem.hpp"
#include "Engine/InferusRenderer/ShaderStageBuilder.hpp"
#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"

namespace TerrainRenderer {

    struct TerrainPushConstants {
        glm::mat4 CameraMVP;
        glm::vec4 PlayerPosition;
    };

    // Push constants
    TerrainPushConstants TerrainPushConstants {};

    namespace Descriptor {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
    };

    namespace PlaneMesh {
        BufferSystem::Id Indices;

        // Easier access for vkBindIndexBuffer()
        VkBuffer VkBuffer;

        void GenerateIndices(uint32_t* IndicesBegin) {
            int32_t TerrainRes = TerrainConfig::Chunk::RESOLUTION;
            for (int z = 0; z < TerrainRes - 1; z++) {
                for (int x = 0; x < TerrainRes - 1; x++) {
                    // Calculate the index of the current vertex and neighbors
                    uint32_t topLeft = (z * TerrainConfig::Chunk::RESOLUTION) + x;
                    uint32_t topRight = topLeft + 1;
                    uint32_t bottomLeft = ((z + 1) * TerrainConfig::Chunk::RESOLUTION) + x;
                    uint32_t bottomRight = bottomLeft + 1;

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
            VkCommandBuffer TransferCmd = VulkanContext::SingleTimeCmdBegin(VulkanContext::Graphics);

            // Create the actual Plane Mesh index buffer
            BufferSystem::CreateInfo IndiceCreateInfo = {
                .size = TerrainConfig::Chunk::INDICES_BUFFER_SIZE,
                .memType = BufferSystem::CreateInfoMemoryType::GPU_STATIC,
                .usage = BufferSystem::CreateInfoUsage::INDEX,
            };
            Indices = BufferSystem::add(IndiceCreateInfo);
            VkBuffer = BufferSystem::get(PlaneMesh::Indices)->buffer;

            // TODO:
            // It's kinda of dumb I keep creating single usage staging buffers
            // Create the staging buffer and copy the data
            BufferSystem::CreateInfo IndicesStagingBufferCreateInfo = {
                .size = TerrainConfig::Chunk::INDICES_BUFFER_SIZE,
                .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
                .usage = BufferSystem::CreateInfoUsage::STAGING,
            };
            BufferSystem::Id StagingIndices = BufferSystem::add(IndicesStagingBufferCreateInfo);
            uint32_t* StagingPlaneMeshIndices = (uint32_t*)BufferSystem::map(StagingIndices);
            GenerateIndices(StagingPlaneMeshIndices);

            BufferSystem::copy(TransferCmd, StagingIndices, Indices, TerrainConfig::Chunk::INDICES_BUFFER_SIZE);

            VulkanContext::SingleTimeCmdSubmit(VulkanContext::Graphics, TransferCmd);

            // Cleanup
            BufferSystem::unmap(StagingIndices);
            BufferSystem::del(StagingIndices);
        }
    }

    namespace Heightmap {
        ImageSystem::Id Image;
        ImageSystem::View::Id ImageView;
        VkSampler Sampler;
        BufferSystem::Id StagingBuffer;
    }

    namespace ChunkLinks {
        BufferSystem::Id Staging;
        BufferSystem::Id Data;
    }

    // Terrain pipeline
    VkPipeline TerrainPipeline {};
    VkPipelineLayout TerrainPipelineLayout {};

    InferusResult Create() {
        VkDevice& Device = VulkanContext::Device;
        {
            VkShaderStageFlags AllStages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

            // Terrain heightmap
            {
                ImageSystem::ImageCreateInfo HeightmapImageCreateDesc;
                HeightmapImageCreateDesc.width = TerrainConfig::Chunk::RESOLUTION;
                HeightmapImageCreateDesc.height = TerrainConfig::Chunk::RESOLUTION;
                HeightmapImageCreateDesc.arrayLayers = TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT;
                HeightmapImageCreateDesc.format = TerrainConfig::Heightmap::HEIGHTMAP_IMAGE_FORMAT;
                HeightmapImageCreateDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

                Heightmap::Image = ImageSystem::add(HeightmapImageCreateDesc);
                ImageSystem::Image* HeightmapImage = ImageSystem::get(Heightmap::Image);
                auto HeightmapImageViewCreateInfo = ImageSystem::fillDefaultImageViewCreateInfo(HeightmapImage);
                Heightmap::ImageView = ImageSystem::View::add(HeightmapImageViewCreateInfo);

                auto HeightmapSamplerInfo = Recipes::SamplerCreateInfo::HeightmapSampler();
                vkCreateSampler(Device, &HeightmapSamplerInfo, nullptr, &Heightmap::Sampler);

                BufferSystem::CreateInfo HeightmapStagingBufferId_CreateInfo = {
                    .size = TerrainConfig::Heightmap::HEIGHTMAP_ALL_IMAGES_SIZE,
                    .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
                    .usage = BufferSystem::CreateInfoUsage::STAGING
                };
                Heightmap::StagingBuffer = BufferSystem::add(HeightmapStagingBufferId_CreateInfo);
            }

            // Chunk to Heightmap linking
            {
                BufferSystem::CreateInfo ChunkHeightmapLinksCPU_CreateDesc = {
                    .size = TerrainConfig::ChunkToHeightmapLinking::LINKING_BUFFER_SIZE,
                    .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
                    .usage = BufferSystem::CreateInfoUsage::STAGING
                };
                BufferSystem::CreateInfo ChunkHeightmapLinksGPU_CreateDesc = {
                    .size = TerrainConfig::ChunkToHeightmapLinking::LINKING_BUFFER_SIZE,
                    .memType = BufferSystem::CreateInfoMemoryType::GPU_STATIC,
                    .usage = BufferSystem::CreateInfoUsage::SSBO
                };

                ChunkLinks::Staging = BufferSystem::add(ChunkHeightmapLinksCPU_CreateDesc);
                ChunkLinks::Data = BufferSystem::add(ChunkHeightmapLinksGPU_CreateDesc);
            }

            // Terrain System Descriptors
            {
                // Heightmap Texture Sampler descriptor
                auto HeightmapImageViewValue = ImageSystem::View::get(Heightmap::ImageView);
                VkDescriptorImageInfo HeightmapTextureDescriptorImageInfo {};
                HeightmapTextureDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                HeightmapTextureDescriptorImageInfo.imageView = HeightmapImageViewValue->imageView;
                HeightmapTextureDescriptorImageInfo.sampler = Heightmap::Sampler;

                VkDescriptorSetLayoutBinding HeightmapSetLayoutBinding {};
                HeightmapSetLayoutBinding.binding = 0;
                HeightmapSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                HeightmapSetLayoutBinding.descriptorCount = 1;
                HeightmapSetLayoutBinding.stageFlags = AllStages;
                HeightmapSetLayoutBinding.pImmutableSamplers = nullptr;

                VkWriteDescriptorSet HeightmapSamplerWrite {};
                HeightmapSamplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                HeightmapSamplerWrite.dstBinding = 0;
                HeightmapSamplerWrite.dstArrayElement = 0;
                HeightmapSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                HeightmapSamplerWrite.descriptorCount = 1;
                HeightmapSamplerWrite.pImageInfo = &HeightmapTextureDescriptorImageInfo;

                // Chunk to Heightmap descriptor
                // TODO: The fact I'm not carrying offsets arround is most definitvelly a bad signal
                auto ChunkLinkBuffer = BufferSystem::get(ChunkLinks::Data);
                VkDescriptorBufferInfo ChunkToHeightmapDescriptorBufferInfo {};
                ChunkToHeightmapDescriptorBufferInfo.buffer = ChunkLinkBuffer->buffer;
                ChunkToHeightmapDescriptorBufferInfo.offset = 0;
                ChunkToHeightmapDescriptorBufferInfo.range = ChunkLinkBuffer->size;

                VkDescriptorSetLayoutBinding ChunkLinkBinding {};
                ChunkLinkBinding.binding = 1;
                ChunkLinkBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                ChunkLinkBinding.descriptorCount = 1;
                ChunkLinkBinding.stageFlags = AllStages;
                ChunkLinkBinding.pImmutableSamplers = nullptr;

                VkWriteDescriptorSet ChunkLinkSSBOWrite {};
                ChunkLinkSSBOWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                ChunkLinkSSBOWrite.dstBinding = 1;
                ChunkLinkSSBOWrite.dstArrayElement = 0;
                ChunkLinkSSBOWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                ChunkLinkSSBOWrite.descriptorCount = 1;
                ChunkLinkSSBOWrite.pBufferInfo = &ChunkToHeightmapDescriptorBufferInfo;

                // Descriptor layout
                std::array<VkDescriptorSetLayoutBinding, 2> LayoutBindings = {
                    HeightmapSetLayoutBinding,
                    ChunkLinkBinding
                };
                VkDescriptorSetLayoutCreateInfo LayoutCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .bindingCount = static_cast<uint32_t>(LayoutBindings.size()),
                    .pBindings = LayoutBindings.data()
                };
                if (
                    vkCreateDescriptorSetLayout(
                        Device,
                        &LayoutCreateInfo,
                        nullptr,
                        &Descriptor::layout
                    ) != VK_SUCCESS
                    )
                {
                    spdlog::error("Terrain descriptor set layout creation failed");
                    return InferusResult::FAIL;
                }

                // Descriptor pool
                VkDescriptorPoolSize SamplerHeightmapPoolSize = {
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1
                };
                VkDescriptorPoolSize SSBOHeightmapPoolSize = {
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1
                };
                std::array<VkDescriptorPoolSize, 2> PoolSize = {
                    SamplerHeightmapPoolSize,
                    SSBOHeightmapPoolSize
                };

                VkDescriptorPoolCreateInfo PoolInfo{};
                PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                PoolInfo.poolSizeCount = static_cast<uint32_t>(PoolSize.size());
                PoolInfo.pPoolSizes = PoolSize.data();
                PoolInfo.maxSets = 1;

                if (vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Descriptor::pool) != VK_SUCCESS) {
                    spdlog::error("Descriptor pool creation failed");
                    return InferusResult::FAIL;
                }

                VkDescriptorSetAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = Descriptor::pool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &Descriptor::layout;

                if (vkAllocateDescriptorSets(Device, &allocInfo, &Descriptor::set) != VK_SUCCESS) {
                    spdlog::error("Descriptor set allocation failed");
                    return InferusResult::FAIL;
                }

                std::array<VkWriteDescriptorSet, 2> TerrainWrites = {
                    HeightmapSamplerWrite, ChunkLinkSSBOWrite
                };
                for (VkWriteDescriptorSet& write : TerrainWrites) {
                    write.dstSet = Descriptor::set;
                }

                vkUpdateDescriptorSets(Device, static_cast<uint32_t>(TerrainWrites.size()), TerrainWrites.data(), 0, nullptr);
            }

            TerrainPipelineLayout = {};
            VkPushConstantRange TerrainPushConstantRange = {
                .stageFlags = AllStages,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(TerrainPushConstants))
            };

            VkPipelineLayoutCreateInfo TerrainPipelineLayoutCreateInfo {};
            TerrainPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            TerrainPipelineLayoutCreateInfo.setLayoutCount = 1;
            TerrainPipelineLayoutCreateInfo.pSetLayouts = &Descriptor::layout;
            TerrainPipelineLayoutCreateInfo.pPushConstantRanges = &TerrainPushConstantRange;
            TerrainPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            if (vkCreatePipelineLayout(Device, &TerrainPipelineLayoutCreateInfo, nullptr, &TerrainPipelineLayout) != VK_SUCCESS) {
                spdlog::error("Terrain pipeline layout creation failed");
                return InferusResult::FAIL;
            }

            // Finally creating the terrain VkPipeline itself
            std::vector<VkDynamicState> DynamicStates {};
            DynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo DynamicState {};
            DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            DynamicState.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
            DynamicState.pDynamicStates = DynamicStates.data();

            std::array<VkFormat, 1> ColorAttachmentFormats = {
                VulkanContext::SurfaceFormat.format,
            };
            auto TerrainColorBlendState = Recipes::Pipeline::Parts::ColorBlendAttachmentState::Default();
            std::vector<VkPipelineColorBlendAttachmentState> TerrainBlendAttachments(ColorAttachmentFormats.size(), TerrainColorBlendState);
            VkPipelineRenderingCreateInfo RenderingCreateInfo {};
            RenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            RenderingCreateInfo.colorAttachmentCount = static_cast<uint32_t>(ColorAttachmentFormats.size());
            RenderingCreateInfo.pColorAttachmentFormats = ColorAttachmentFormats.data();
            RenderingCreateInfo.depthAttachmentFormat = RendererConfig::DepthBuffer::Format;
            RenderingCreateInfo.stencilAttachmentFormat = RendererConfig::DepthBuffer::Format;

            auto VertexInput = Recipes::Pipeline::Parts::VertexInput::Default();
            auto InputAssembly = Recipes::Pipeline::Parts::InputAssembly::Default();
            auto ViewportState = Recipes::Pipeline::Parts::ViewportState::Default();
            auto Rasterization = Recipes::Pipeline::Parts::Rasterization::Default();
            auto Multisample = Recipes::Pipeline::Parts::Multisample::Default();
            auto DepthStencil = Recipes::Pipeline::Parts::DepthStencil::Default();
            auto ColorBlendState = Recipes::Pipeline::Parts::ColorBlendState::Default(TerrainBlendAttachments);

            VkGraphicsPipelineCreateInfo TerrainPipelineCreateInfo {};
            TerrainPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            TerrainPipelineCreateInfo.pVertexInputState = &VertexInput;
            TerrainPipelineCreateInfo.pInputAssemblyState = &InputAssembly;
            TerrainPipelineCreateInfo.pViewportState = &ViewportState;
            TerrainPipelineCreateInfo.pRasterizationState = &Rasterization;
            TerrainPipelineCreateInfo.pMultisampleState = &Multisample;
            TerrainPipelineCreateInfo.pDepthStencilState = &DepthStencil;
            TerrainPipelineCreateInfo.pColorBlendState = &ColorBlendState;
            TerrainPipelineCreateInfo.pDynamicState = &DynamicState;
            TerrainPipelineCreateInfo.layout = TerrainPipelineLayout;
            TerrainPipelineCreateInfo.basePipelineIndex = -1;
            TerrainPipelineCreateInfo.pNext = &RenderingCreateInfo;

            // Add shaders
            std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
            std::vector<char> ShaderBuffer;
            ShaderBuffer.reserve(4096);

            ShaderStages.push_back(
                ShaderBuilder::CreateShaderStage(
                    VK_SHADER_STAGE_VERTEX_BIT,
                    "shaders/terrain.vert.spv",
                    ShaderBuffer,
                    Device
                )
            );
            ShaderStages.push_back(
                ShaderBuilder::CreateShaderStage(
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    "shaders/terrain.frag.spv",
                    ShaderBuffer,
                    Device
                )
            );

            TerrainPipelineCreateInfo.stageCount = static_cast<uint32_t>(ShaderStages.size());
            TerrainPipelineCreateInfo.pStages = ShaderStages.data();

            if (
                vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &TerrainPipelineCreateInfo, nullptr, &TerrainPipeline) != VK_SUCCESS
                ) {
                spdlog::error("Terrain Pipeline creation failed.");
                return InferusResult::FAIL;
            }

            // TODO: Add caching
            // As of now just destroy the shader modules
            for (auto ShaderStage : ShaderStages) {
                if (ShaderStage.module) { vkDestroyShaderModule(Device, ShaderStage.module, nullptr); }
            }
        }

        PlaneMesh::Upload();

        // Zeroing terrain push constants
        TerrainPushConstants = {
            .CameraMVP = glm::mat4(0),
            .PlayerPosition = glm::vec4(0)
        };

        return InferusResult::SUCCESS;
    }

    void Destroy() {
        VkDevice& Device = VulkanContext::Device;

        BufferSystem::del(ChunkLinks::Staging);
        BufferSystem::del(ChunkLinks::Data);

        BufferSystem::del(Heightmap::StagingBuffer);

        BufferSystem::del(PlaneMesh::Indices);

        if (Heightmap::Sampler) { vkDestroySampler(Device, Heightmap::Sampler, nullptr); }
        ImageSystem::del(Heightmap::Image);

        if (Descriptor::pool) { vkDestroyDescriptorPool(Device, Descriptor::pool, nullptr); }
        if (Descriptor::layout) { vkDestroyDescriptorSetLayout(Device, Descriptor::layout, nullptr); }

        if (TerrainPipeline) { vkDestroyPipeline(Device, TerrainPipeline, nullptr); }
        if (TerrainPipelineLayout) { vkDestroyPipelineLayout(Device, TerrainPipelineLayout, nullptr); }
    }

    void BindCamera(Camera::Camera3D &Camera) {
        Camera.ModelViewProjection = &TerrainPushConstants.CameraMVP;
    }

void FeedTerrainSystemPointers() {
        QueueContext& Graphics = VulkanContext::Graphics;

        // to remove btw
        TerrainSystem::FeedTerrainRenderer(
            (ChunkHeightmapLink*)BufferSystem::map(ChunkLinks::Staging),
            (uint16_t*)BufferSystem::map(Heightmap::StagingBuffer)
        );
        TerrainSystem::FeedTerrainData(ChunkLinks::Data, Heightmap::Image);

        VkCommandBuffer cmd = VulkanContext::SingleTimeCmdBegin(Graphics);

        BufferSystem::copy(
            cmd,
            ChunkLinks::Staging,
            ChunkLinks::Data,
            TerrainConfig::ChunkToHeightmapLinking::LINKING_BUFFER_SIZE
        );
        BufferSystem::unmap(ChunkLinks::Staging);
        BufferSystem::unmap(Heightmap::StagingBuffer);

        BufferSystem::Buffer* HeightmapStagingBuffer = BufferSystem::get(Heightmap::StagingBuffer);
        ImageSystem::Image* HeightmapImage = ImageSystem::get(Heightmap::Image);

        VkImageMemoryBarrier barrier1 = Recipes::ImageMemoryBarrier::TransferDest(HeightmapImage);
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        vkCmdPipelineBarrier(
            cmd,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier1
        );

        VkBufferImageCopy HeightmapCopy = Recipes::BufferImageCopy::Default(HeightmapImage);

        vkCmdCopyBufferToImage(
            cmd,
            HeightmapStagingBuffer->buffer,
            HeightmapImage->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &HeightmapCopy
        );

        VkImageMemoryBarrier barrier2 = Recipes::ImageMemoryBarrier::ShaderRead(HeightmapImage);
        // Assuming your Recipes struct sets src/dstQueueFamilyIndex to VK_QUEUE_FAMILY_IGNORED by default.
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        vkCmdPipelineBarrier(
            cmd,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier2
        );

        VulkanContext::SingleTimeCmdSubmit(Graphics, cmd);
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

        vkCmdDrawIndexed(cmd, TerrainConfig::Chunk::INDICES_COUNT, TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT, 0, 0, 0);
    }
}
