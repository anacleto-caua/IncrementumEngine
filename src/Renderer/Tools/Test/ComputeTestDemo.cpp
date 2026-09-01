#include "ComputeTestDemo.hpp"

#include <cassert>
#include <cstring>
#include <vector>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Resources/Buffer.hpp"
#include "Renderer/Tools/ComputePipe.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

namespace ComputeTestDemo {
    struct PushConstants {
        u32 Multiplier;
        u32 Offset;
    };

    BufferId TestBuffer;
    VkDescriptorSet TestDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout TestPipelineLayout = VK_NULL_HANDLE;
    VkPipeline TestPipeline = VK_NULL_HANDLE;

    IncResult Init() {
        Buffer::CreateInfo buffer_create_info = {
            .Size = TEST_ELEMENT_COUNT * sizeof(u32),
            .Type = Buffer::Type::SSBO,
        };
        INC_CHECK(Buffers.Add(buffer_create_info, TestBuffer), "compute test buffer creation failed");

        // Descriptor set
        TestDescriptorSet = DescriptorManager::AllocateSet(DescriptorManager::ComputeTestLayout);

        Buffer* test_buffer_value = Buffers.Get(TestBuffer);
        VkDescriptorBufferInfo test_buffer_info {};
        test_buffer_info.buffer = test_buffer_value->Handle;
        test_buffer_info.offset = 0;
        test_buffer_info.range = test_buffer_value->Size;

        VkWriteDescriptorSet test_buffer_write {};
        test_buffer_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        test_buffer_write.dstSet = TestDescriptorSet;
        test_buffer_write.dstBinding = DescriptorMap::ComputeTest::Binding_TestBuffer;
        test_buffer_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        test_buffer_write.descriptorCount = 1;
        test_buffer_write.pBufferInfo = &test_buffer_info;

        vkUpdateDescriptorSets(VkVault::Device, 1, &test_buffer_write, 0, nullptr);

        // Pipeline layout
        VkPushConstantRange push_constant_range {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo pipeline_layout_create_info {};
        pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts = &DescriptorManager::ComputeTestLayout;
        pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
        pipeline_layout_create_info.pushConstantRangeCount = 1;

        VK_CHECK(
            vkCreatePipelineLayout(VkVault::Device, &pipeline_layout_create_info, nullptr, &TestPipelineLayout),
            "compute test pipeline layout creation failed"
        );

        // Pipeline
        VkPipelineShaderStageCreateInfo compute_shader_stage;
        std::vector<u32> shader_buffer;

        INC_CHECK(
            CreateShaderStage(
                VK_SHADER_STAGE_COMPUTE_BIT,
                "shaders/compute_test.comp.spv",
                shader_buffer,
                compute_shader_stage
            ),
            "compute test shader creation failed"
        );

        VkComputePipelineCreateInfo compute_pipeline_create_info {};
        compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        compute_pipeline_create_info.stage = compute_shader_stage;
        compute_pipeline_create_info.layout = TestPipelineLayout;
        compute_pipeline_create_info.basePipelineIndex = -1;

        VK_CHECK(
            vkCreateComputePipelines(
                VkVault::Device,
                VK_NULL_HANDLE,
                1,
                &compute_pipeline_create_info,
                nullptr,
                &TestPipeline
            ),
            "compute test pipeline creation failed"
        );

        if (compute_shader_stage.module) { vkDestroyShaderModule(VkVault::Device, compute_shader_stage.module, nullptr); }

        // Dispatch, flush, and verify
        ComputeDispatchDesc dispatch_desc {};
        dispatch_desc.Pipeline = TestPipeline;
        dispatch_desc.Layout = TestPipelineLayout;
        dispatch_desc.DescriptorSets[0] = TestDescriptorSet;
        dispatch_desc.DescriptorSetCount = 1;
        dispatch_desc.GroupCountX = TEST_ELEMENT_COUNT / 64;

        PushConstants push_constants = { .Multiplier = TEST_MULTIPLIER, .Offset = TEST_OFFSET };
        static_assert(sizeof(PushConstants) <= COMPUTE_MAX_PUSH_CONSTANT_BYTES);
        dispatch_desc.PushConstantSize = sizeof(PushConstants);
        std::memcpy(dispatch_desc.PushConstantData.data(), &push_constants, sizeof(PushConstants));

        Ticket dispatch_ticket = GComputePipe.QueueDispatch(dispatch_desc);
        GComputePipe.LazySubmit();
        dispatch_ticket.WaitOn();

        u32* mapped = static_cast<u32*>(Buffers.Map(TestBuffer));
        bool all_correct = true;
        for (u32 i = 0; i < TEST_ELEMENT_COUNT; i++) {
            if (mapped[i] != i * TEST_MULTIPLIER + TEST_OFFSET) {
                all_correct = false;
                break;
            }
        }
        Buffers.Unmap(TestBuffer);

        assert(all_correct && "compute test dispatch produced incorrect results");
        if (all_correct) {
            analog::info("ComputeTestDemo: compute dispatch verified, {} values correct", TEST_ELEMENT_COUNT);
        } else {
            analog::error("ComputeTestDemo: compute dispatch verification FAILED");
        }

        return IncResult::SUCCESS;
    }

    void Destroy() {
        if (TestPipeline) { vkDestroyPipeline(VkVault::Device, TestPipeline, nullptr); }
        if (TestPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, TestPipelineLayout, nullptr); }
        Buffers.Del(TestBuffer);
    }
}
