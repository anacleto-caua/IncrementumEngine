#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "Engine/InferusRenderer/Image/ImageSystem.hpp"

#define RECIPE static inline

namespace Recipes {
    namespace Pipeline {
        namespace Parts {
            namespace VertexInput {
                RECIPE VkPipelineVertexInputStateCreateInfo Default() {
                    VkPipelineVertexInputStateCreateInfo VertexInputInfo {};
                    VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                    VertexInputInfo.vertexBindingDescriptionCount = 0;
                    VertexInputInfo.pVertexBindingDescriptions = nullptr;
                    VertexInputInfo.vertexAttributeDescriptionCount = 0;
                    VertexInputInfo.pVertexAttributeDescriptions = nullptr;
                    return VertexInputInfo;
                }
            };
            namespace InputAssembly {
                RECIPE VkPipelineInputAssemblyStateCreateInfo Default() {
                    VkPipelineInputAssemblyStateCreateInfo InputAssembly {};
                    InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    InputAssembly.primitiveRestartEnable = VK_FALSE;
                    return InputAssembly;
                }
            };
            namespace Rasterization {
                RECIPE VkPipelineRasterizationStateCreateInfo Default() {
                    VkPipelineRasterizationStateCreateInfo Rasterizer {};
                    Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                    Rasterizer.depthClampEnable = VK_FALSE;
                    Rasterizer.rasterizerDiscardEnable = VK_FALSE;
                    Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                    Rasterizer.lineWidth = 1.0f;
                    Rasterizer.cullMode = VK_CULL_MODE_NONE;
                    Rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
                    Rasterizer.depthBiasEnable = VK_FALSE;
                    Rasterizer.depthBiasConstantFactor = 0.0f;
                    Rasterizer.depthBiasClamp = 0.0f;
                    Rasterizer.depthBiasSlopeFactor = 0.0f;
                    return Rasterizer;
                }
            };
            namespace Multisample {
                RECIPE VkPipelineMultisampleStateCreateInfo Default() {
                    VkPipelineMultisampleStateCreateInfo Multisampling {};
                    Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                    Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                    Multisampling.pSampleMask = nullptr;
                    Multisampling.alphaToCoverageEnable = VK_FALSE;
                    Multisampling.alphaToOneEnable = VK_FALSE;
                    Multisampling.sampleShadingEnable = VK_TRUE;
                    Multisampling.minSampleShading = .2f;
                    return Multisampling;
                }
            };
            namespace DepthStencil {
                RECIPE VkPipelineDepthStencilStateCreateInfo Default() {
                    VkPipelineDepthStencilStateCreateInfo DepthStencil {};
                    DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                    DepthStencil.depthTestEnable = VK_TRUE;
                    DepthStencil.depthWriteEnable = VK_TRUE;
                    DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                    DepthStencil.depthBoundsTestEnable = VK_FALSE;
                    DepthStencil.minDepthBounds = 0.0f;
                    DepthStencil.maxDepthBounds = 1.0f;
                    DepthStencil.stencilTestEnable = VK_FALSE;
                    DepthStencil.front = {};
                    DepthStencil.back = {};
                    return DepthStencil;
                }
            };
            namespace ViewportState {
                RECIPE VkPipelineViewportStateCreateInfo Default() {
                    VkPipelineViewportStateCreateInfo ViewportState{};
                    ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                    ViewportState.viewportCount = 1;
                    ViewportState.scissorCount = 1;
                    return ViewportState;
                }
            };
            namespace ColorBlendAttachmentState {
                RECIPE VkPipelineColorBlendAttachmentState Default() {
                    VkPipelineColorBlendAttachmentState ColorBlendState{};
                    ColorBlendState.colorWriteMask =
                        VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT;
                    ColorBlendState.blendEnable = VK_TRUE;
                    ColorBlendState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                    ColorBlendState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    ColorBlendState.colorBlendOp = VK_BLEND_OP_ADD;
                    ColorBlendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                    ColorBlendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                    ColorBlendState.alphaBlendOp = VK_BLEND_OP_ADD;
                    return ColorBlendState;
                }
            };
            namespace ColorBlendState {
                RECIPE VkPipelineColorBlendStateCreateInfo Default(
                        VkPipelineColorBlendAttachmentState* BlendAttachments,
                        uint32_t Size)
                {
                    VkPipelineColorBlendStateCreateInfo ColorBlending{};
                    ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                    ColorBlending.logicOpEnable = VK_FALSE;
                    ColorBlending.logicOp = VK_LOGIC_OP_COPY;
                    ColorBlending.blendConstants[0] = 0.0f;
                    ColorBlending.blendConstants[1] = 0.0f;
                    ColorBlending.blendConstants[2] = 0.0f;
                    ColorBlending.blendConstants[3] = 0.0f;
                    ColorBlending.attachmentCount = Size;
                    ColorBlending.pAttachments = BlendAttachments;
                    return ColorBlending;
                }
                RECIPE VkPipelineColorBlendStateCreateInfo Default(std::vector<VkPipelineColorBlendAttachmentState> &States){
                    return Default(States.data(), States.size());
                }
            };
        };
    };
    namespace ColorAttachment {
        RECIPE VkRenderingAttachmentInfo Default() {
            VkRenderingAttachmentInfo RenderingInfo = {};
            RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            RenderingInfo.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            RenderingInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            RenderingInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            RenderingInfo.clearValue.color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } };

            return RenderingInfo;
        }

        RECIPE VkRenderingAttachmentInfo Terrain() {
            return Default();
        }
    };
    namespace DepthAttachment {
        RECIPE VkRenderingAttachmentInfo Default() {
            VkRenderingAttachmentInfo depthAttachmentInfo{};
            depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachmentInfo.imageView = VK_NULL_HANDLE; // MUST BE SET BY YOU
            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachmentInfo.clearValue.depthStencil = { 1.0f, 0 };

            return depthAttachmentInfo;
        }
    };
    namespace ImageViewCreateInfo {
        RECIPE VkImageViewCreateInfo Default(VkImage Image, VkFormat Format) {
            return {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = Image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = Format,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
        }

        RECIPE VkImageViewCreateInfo Swapchain(VkImage Image, VkFormat Format) {
            return Default(Image, Format);
        }
    };
    namespace SamplerCreateInfo {
        RECIPE VkSamplerCreateInfo HeightmapSampler() {
            VkSamplerCreateInfo samplerInfo {};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 0.0f;
            return samplerInfo;
        };
    };
    namespace BufferImageCopy {
       RECIPE VkBufferImageCopy Default(const ImageSystem::Image* image) {
            VkBufferImageCopy imageCopy {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = image->arrayLayers
                },
                .imageOffset = {0, 0, 0},
                .imageExtent = {image->width, image->height, 1}
            };
            return imageCopy;
       }
    };
    namespace ImageMemoryBarrier {
        RECIPE VkImageMemoryBarrier RawDefault(VkImage image) {
            VkImageMemoryBarrier Barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .srcQueueFamilyIndex =VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };
            return Barrier;
        }

        RECIPE VkImageMemoryBarrier Default(const ImageSystem::Image* image) {
            VkImageMemoryBarrier barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = 0,
                .oldLayout = image->layout,
                .newLayout = image->layout,
                .srcQueueFamilyIndex =VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image->image,
                .subresourceRange {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = image->mipLevels,
                    .baseArrayLayer = 0,
                    .layerCount = image->arrayLayers,
                }
            };
            return barrier;
        }

        RECIPE VkImageMemoryBarrier TransferDest(const ImageSystem::Image* image) {
            VkImageMemoryBarrier barrier = Default(image);
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            return barrier;
        }

        RECIPE VkImageMemoryBarrier ShaderRead(const ImageSystem::Image* image) {
            VkImageMemoryBarrier barrier = Default(image);
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            return barrier;
        }

        namespace DepthBuffer {
            RECIPE VkImageMemoryBarrier MakeValid(const ImageSystem::Image *image, const VkImageSubresourceRange& subRsr) {
                VkImageMemoryBarrier barrier = Default(image);
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                barrier.subresourceRange = subRsr;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                return barrier;
            }
        };

        namespace Rendering {
            RECIPE VkImageMemoryBarrier EnableRendering(VkImage Image) {
                VkImageMemoryBarrier Barrier = RawDefault(Image);
                Barrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                Barrier.srcAccessMask = 0;
                Barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                return Barrier;
            };

            RECIPE VkImageMemoryBarrier EnablePresenting(VkImage Image) {
                VkImageMemoryBarrier Barrier = RawDefault(Image);
                Barrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                Barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                Barrier.dstAccessMask = 0;
                return Barrier;
            };
        };
    };
};
