#pragma once

#include <cstdint>
#include <functional>

#include <vulkan/vulkan.h>

#include "Engine/InferusRenderer/Image/ImageSystem.hpp"
#include "Engine/InferusRenderer/Buffer/BufferSystem.hpp"

/**
 * While the first 4 functions are more aimed for Renderer's usage, the later ones are
 * meant to be used by other systems(like the terrain one) so I feel like I should split it eventually.
 */
namespace TransferSystem {
    using UploadReaction = std::function<void()>;

    void Create();
    /**
     * This function should auto update the available gpu bandwith
     * using frame time and something akin to AIMD as of now it's not being used
     */
    // void TickAndFlush(float lastFrameTime);
    void FrameTransfer(VkCommandBuffer cmd);
    void FrameReactions();
    void Destroy();

    // vkCmdUpdateBuffer
    void QueueBufferUpdate(BufferSystem::Id dst, const void* data, uint64_t size, uint64_t offset);
    void QueueBufferUpdate(BufferSystem::Id dst, const void* data, uint64_t size, uint64_t offset, UploadReaction reaction);

    // vkCmdBufferImageCopy
    void QueueImageSliceUpdate(ImageSystem::Id dst, const void* data, uint32_t bytes_per_pixel, uint32_t target_layer, uint64_t size);
    void QueueImageSliceUpdate(ImageSystem::Id dst, const void* data, uint32_t bytes_per_pixel, uint32_t target_layer, uint64_t size, UploadReaction reaction);

    /*
    // vkCmdBufferCopy
    void QueueBufferUpload(BufferSystem::Id* dst, const void* data, uint64_t size);
    void QueueBufferUpload(BufferSystem::Id* dst, const void* data, uint64_t size, UploadReaction reaction);
    */
}
