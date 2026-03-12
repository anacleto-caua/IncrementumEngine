#include "TerrainRenderer.hpp"

#include <spdlog/spdlog.h>

#include "Engine/InferusRenderer/Recipes.hpp"
#include "Engine/Systems/Terrain/TerrainConfig.hpp"
#include "Engine/Systems/Terrain/TerrainConfig.hpp"
#include "Engine/InferusRenderer/VulkanContext.hpp"
#include "Engine/Systems/Terrain/TerrainSystem.hpp"
#include "Engine/InferusRenderer/Image/ImageSystem.hpp"
#include "Engine/InferusRenderer/ShaderStageBuilder.hpp"
#include "Engine/Systems/Terrain/PlaneMeshIndicesGenerator.hpp"

namespace TerrainRenderer {
    // Terrain plane mesh
    BufferSystem::Id PlaneMeshIndexBufferId;
    VkBuffer PlaneMeshIndexVkBuffer;

    // Terrain pipeline
    VkPipeline TerrainPipeline {};
    VkPipelineLayout TerrainPipelineLayout {};

    // Heightmap
    ImageSystem::Id HeightmapImageId;
    VkSampler HeightmapTextureSampler;
    BufferSystem::Id Heightmap_CPU;

    // Chunk to Heightmap linking
    ChunkHeightmapLink ChunkHeightmapLinks[TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT];
    BufferSystem::Id ChunkHeightmapLinks_CPU;
    BufferSystem::Id ChunkHeightmapLinks_GPU;

    // Terrain descriptor sets
    TerrainDescriptorSet TerrainDescriptorSet {};

    // Push constants
    TerrainPushConstants TerrainPushConstants {};

    InferusResult Create(BufferSystem::Id &CreationWiseStagingBuffer) {

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

                HeightmapImageId = ImageSystem::add(HeightmapImageCreateDesc);

                auto HeightmapSamplerInfo = Recipes::SamplerCreateInfo::HeightmapSampler();
                vkCreateSampler(Device, &HeightmapSamplerInfo, nullptr, &HeightmapTextureSampler);

                BufferSystem::CreateInfo HeightmapStagingBufferId_CreateInfo = {
                    .size = TerrainConfig::Heightmap::HEIGHTMAP_ALL_IMAGES_SIZE,
                    .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
                    .usage = BufferSystem::CreateInfoUsage::STAGING
                };
                Heightmap_CPU = BufferSystem::add(HeightmapStagingBufferId_CreateInfo);
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

                ChunkHeightmapLinks_CPU = BufferSystem::add(ChunkHeightmapLinksCPU_CreateDesc);
                ChunkHeightmapLinks_GPU = BufferSystem::add(ChunkHeightmapLinksGPU_CreateDesc);
            }

            // Terrain System Descriptors
            {
                // Heightmap Texture Sampler descriptor
                auto HeightmapImage = ImageSystem::get(HeightmapImageId);
                VkDescriptorImageInfo HeightmapTextureDescriptorImageInfo {};
                HeightmapTextureDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                HeightmapTextureDescriptorImageInfo.imageView = HeightmapImage.imageView;
                HeightmapTextureDescriptorImageInfo.sampler = HeightmapTextureSampler;

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
                auto ChunkLinkBuffer = BufferSystem::get(ChunkHeightmapLinks_GPU);
                VkDescriptorBufferInfo ChunkToHeightmapDescriptorBufferInfo {};
                ChunkToHeightmapDescriptorBufferInfo.buffer = ChunkLinkBuffer.buffer;
                ChunkToHeightmapDescriptorBufferInfo.offset = 0;
                ChunkToHeightmapDescriptorBufferInfo.range = ChunkLinkBuffer.size;

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
                VkDescriptorSetLayoutCreateInfo TerrainDescriptorSetLayoutCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .bindingCount = static_cast<uint32_t>(LayoutBindings.size()),
                    .pBindings = LayoutBindings.data()
                };
                if (
                    vkCreateDescriptorSetLayout(
                        Device,
                        &TerrainDescriptorSetLayoutCreateInfo,
                        nullptr,
                        &TerrainDescriptorSet.layout
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

                if (vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &TerrainDescriptorSet.pool) != VK_SUCCESS) {
                    spdlog::error("Descriptor pool creation failed");
                    return InferusResult::FAIL;
                }

                VkDescriptorSetAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = TerrainDescriptorSet.pool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &TerrainDescriptorSet.layout;

                if (vkAllocateDescriptorSets(Device, &allocInfo, &TerrainDescriptorSet.set) != VK_SUCCESS) {
                    spdlog::error("Descriptor set allocation failed");
                    return InferusResult::FAIL;
                }

                std::array<VkWriteDescriptorSet, 2> TerrainWrites = {
                    HeightmapSamplerWrite, ChunkLinkSSBOWrite
                };
                for (VkWriteDescriptorSet& write : TerrainWrites) {
                    write.dstSet = TerrainDescriptorSet.set;
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
            TerrainPipelineLayoutCreateInfo.pSetLayouts = &TerrainDescriptorSet.layout;
            TerrainPipelineLayoutCreateInfo.pPushConstantRanges = &TerrainPushConstantRange;
            TerrainPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            if (vkCreatePipelineLayout(Device, &TerrainPipelineLayoutCreateInfo, nullptr, &TerrainPipelineLayout) != VK_SUCCESS) {
                spdlog::error("Terrain pipeline layout creation failed");
                return InferusResult::FAIL;
            }

            // Finally creating the terrain VkPipeline itself
            // TODO: Check if it's needed since we're already using dynamic rendering
            std::vector<VkDynamicState> DynamicStates {};
            DynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo DynamicState {};
            DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            DynamicState.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
            DynamicState.pDynamicStates = DynamicStates.data();

            // TODO: Do I need a depth attachment?
            // Can I use this instead of picking chunks in order? Compare performance
            VkFormat DepthAttachmentFormat = VK_FORMAT_UNDEFINED;

            std::array<VkFormat, 1> ColorAttachmentFormats = { VulkanContext::SurfaceFormat.format };
            auto TerrainColorBlendState = Recipes::Pipeline::Parts::ColorBlendAttachmentState::Default();
            std::vector<VkPipelineColorBlendAttachmentState> TerrainBlendAttachments(ColorAttachmentFormats.size(), TerrainColorBlendState);
            VkPipelineRenderingCreateInfo RenderingCreateInfo{};
            RenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            RenderingCreateInfo.colorAttachmentCount = static_cast<uint32_t>(ColorAttachmentFormats.size());
            RenderingCreateInfo.pColorAttachmentFormats = ColorAttachmentFormats.data();
            RenderingCreateInfo.depthAttachmentFormat = DepthAttachmentFormat;
            RenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

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

        // --- Creation wise command buffer begins
        VkCommandBuffer TransferCmd = VulkanContext::SingleTimeCmdBegin(VulkanContext::Transfer);

        // Terrain plane mesh indices buffer
        std::array<uint32_t, TerrainConfig::Chunk::INDICES_COUNT> TerrainPlaneMeshIndices;
        PlaneMeshIndicesGenerator::GetIndices(TerrainPlaneMeshIndices.data());

        BufferSystem::CreateInfo PlaneMeshIndexBufferCreateDescription = {
            .size = TerrainConfig::Chunk::INDICES_BUFFER_SIZE,
            .memType = BufferSystem::CreateInfoMemoryType::GPU_STATIC,
            .usage = BufferSystem::CreateInfoUsage::INDEX,
        };
        PlaneMeshIndexBufferId = BufferSystem::add(PlaneMeshIndexBufferCreateDescription);
        PlaneMeshIndexVkBuffer = BufferSystem::get(PlaneMeshIndexBufferId).buffer;

        BufferSystem::upload(
            TransferCmd,
            CreationWiseStagingBuffer,
            PlaneMeshIndexBufferId,
            TerrainPlaneMeshIndices.data(),
            TerrainConfig::Chunk::INDICES_BUFFER_SIZE
            );

        // --- Creation wise command buffer ends
        VulkanContext::SingleTimeCmdSubmit(VulkanContext::Transfer, TransferCmd);

        // Zeroing terrain push constants
        TerrainPushConstants = {
            .CameraMVP = glm::mat4(0),
            .PlayerPosition = glm::vec4(0)
        };

        BufferSystem::del(CreationWiseStagingBuffer);

        return InferusResult::SUCCESS;
    }

    void Destroy() {
        VkDevice& Device = VulkanContext::Device;

        BufferSystem::del(ChunkHeightmapLinks_CPU);
        BufferSystem::del(ChunkHeightmapLinks_GPU);

        BufferSystem::del(Heightmap_CPU);

        BufferSystem::del(PlaneMeshIndexBufferId);

        if (HeightmapTextureSampler) { vkDestroySampler(Device, HeightmapTextureSampler, nullptr); }
        ImageSystem::del(HeightmapImageId);

        if (TerrainDescriptorSet.pool) { vkDestroyDescriptorPool(Device, TerrainDescriptorSet.pool, nullptr); }
        if (TerrainDescriptorSet.layout) { vkDestroyDescriptorSetLayout(Device, TerrainDescriptorSet.layout, nullptr); }

        if (TerrainPipeline) { vkDestroyPipeline(Device, TerrainPipeline, nullptr); }
        if (TerrainPipelineLayout) { vkDestroyPipelineLayout(Device, TerrainPipelineLayout, nullptr); }
    }

    void BindCamera(Camera::Camera3D &Camera) {
        Camera.ModelViewProjection = &TerrainPushConstants.CameraMVP;
    }

    void FeedTerrainSystemPointers() {
        QueueContext& Transfer = VulkanContext::Transfer;
        QueueContext& Graphics = VulkanContext::Graphics;

        TerrainSystem::FeedTerrainRenderer(
            (ChunkHeightmapLink*)BufferSystem::map(ChunkHeightmapLinks_CPU),
            (uint16_t*)BufferSystem::map(Heightmap_CPU)
        );

        VkCommandBuffer cmd = VulkanContext::SingleTimeCmdBegin(Transfer);

        // Copy chunk link buffer
        BufferSystem::copy(
            cmd,
            ChunkHeightmapLinks_CPU,
            ChunkHeightmapLinks_GPU,
            TerrainConfig::ChunkToHeightmapLinking::LINKING_BUFFER_SIZE
        );
        BufferSystem::unmap(ChunkHeightmapLinks_CPU);
        BufferSystem::unmap(Heightmap_CPU);

        // Upload heightmap to staging buffer
        BufferSystem::Buffer HeightmapStagingBuffer = BufferSystem::get(Heightmap_CPU);
        ImageSystem::Image HeightmapImage = ImageSystem::get(HeightmapImageId);

        // Transfer data to heightmap image
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
            HeightmapStagingBuffer.buffer,
            HeightmapImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &HeightmapCopy
        );

        VkImageMemoryBarrier barrier2 = Recipes::ImageMemoryBarrier::ShaderRead(HeightmapImage);
        barrier2.srcQueueFamilyIndex = Transfer.Index;
        barrier2.dstQueueFamilyIndex = Graphics.Index;
        barrier2.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier2
        );

        VulkanContext::SingleTimeCmdSubmit(Transfer, cmd);

        cmd = VulkanContext::SingleTimeCmdBegin(Graphics);

        VkImageMemoryBarrier barrier3 = Recipes::ImageMemoryBarrier::ShaderRead(HeightmapImage);
        barrier3.srcQueueFamilyIndex = Transfer.Index;
        barrier3.dstQueueFamilyIndex = Graphics.Index;
        barrier3.srcAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        vkCmdPipelineBarrier(
            cmd,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier3
        );

        VulkanContext::SingleTimeCmdSubmit(Graphics, cmd);
    }

    void Render(VkCommandBuffer cmd) {
        // TODO: I'm quite unsure on what would be the best way of handling this
        //vkCmdBindIndexBuffer(cmd, BufferSystem.get(Terrain_PlaneMeshIndexBufferId).buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindIndexBuffer(cmd, PlaneMeshIndexVkBuffer, 0, VK_INDEX_TYPE_UINT32);

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
            &TerrainDescriptorSet.set,
            0,
            nullptr
        );

        vkCmdDrawIndexed(cmd, TerrainConfig::Chunk::INDICES_COUNT, TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT, 0, 0, 0);
    }
}
