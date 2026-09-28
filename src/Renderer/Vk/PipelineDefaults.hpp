#pragma once

#include <array>

#include "Renderer/VkVault.hpp"
#include "Renderer/RendererConstants.hpp"

namespace PipelineDefaults {
    // --- Render-target shape. Every pipeline is drawn into exactly one of two rendering scopes
    // per frame (see Renderer::Frame()), and its rendering-create-info + multisample state must
    // match that scope's attachments exactly. ---

    // Scene scope: HDR, multisampled color + multisampled depth (TerrainPass/PropPass/SkyPass).
    inline VkPipelineRenderingCreateInfo SceneRenderingCreateInfo() {
        static constexpr VkFormat ColorFormat = RendererConstants::SceneColorFormat;
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &ColorFormat,
            .depthAttachmentFormat = RendererConstants::DepthBufferFormat,
            .stencilAttachmentFormat = RendererConstants::DepthBufferFormat
        };
    }

    // Overlay scope: the swapchain image itself, single-sampled, no depth attachment at all
    // (PostPass/TextPass/ImGuiPass - all screen-space).
    inline VkPipelineRenderingCreateInfo OverlayRenderingCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = static_cast<u32>(VkVault::ColorAttachmentFormats.size()),
            .pColorAttachmentFormats = VkVault::ColorAttachmentFormats.data(),
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
        };
    }

    // No per-sample shading: MSAA's job here is geometric edges; running every fragment shader
    // (the sky's raymarch included) 4x per pixel would cost far more than it buys.
    inline const VkPipelineMultisampleStateCreateInfo SceneMultisampleStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = RendererConstants::SceneSampleCount,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };
    }

    inline const VkPipelineVertexInputStateCreateInfo DefaultPipelineVertexInputStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = nullptr
        };
    }

    inline const VkPipelineInputAssemblyStateCreateInfo DefaultPipelineInputAssemblyStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };
    }

    inline const VkPipelineRasterizationStateCreateInfo DefaultPipelineRasterizationStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f
        };
    }

    inline const VkPipelineMultisampleStateCreateInfo DefaultPipelineMultisampleStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_TRUE,
            .minSampleShading = 0.2f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };
    }

    inline const VkPipelineDepthStencilStateCreateInfo DefaultPipelineDepthStencilStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f
        };
    }

    inline const VkPipelineColorBlendStateCreateInfo DefaultPipelineColorBlendStateCreateInfo() {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = VkVault::ColorBlendAttachmentState.size(),
            .pAttachments = VkVault::ColorBlendAttachmentState.data(),
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
        };
    }

    // Every pass dynamically sets viewport/scissor per frame (the swapchain can resize) rather
    // than baking them into the pipeline - shared here instead of duplicated per pass. Safe to
    // return a pointer into the function-local `static` array below since static storage duration
    // outlives the call.
    inline const VkPipelineDynamicStateCreateInfo DefaultPipelineDynamicStateCreateInfo() {
        static constexpr std::array<VkDynamicState, 2> DynamicStates = {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = static_cast<u32>(DynamicStates.size()),
            .pDynamicStates = DynamicStates.data()
        };
    }

    inline const VkPipelineViewportStateCreateInfo DefaultPipelineViewportStateCreateInfo() {
        static const VkViewport DummyViewport = {0.0f, 0.0f, 100.0f, 100.0f, 0.0f, 1.0f};
        static const VkRect2D DummyScissor = {{0, 0}, {100, 100}};
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &DummyViewport ,  // Required field
            .scissorCount = 1,
            .pScissors = &DummyScissor      // Required field
        };
    }
}
