#pragma once

#include <array>

#include <glm/fwd.hpp>

#include <vulkan/vulkan.h>

#include "Pass.hpp"
#include "Renderer/RendererConstants.hpp"
#include "Renderer/Resources/Buffer.hpp"
#include "Renderer/Resources/Image.hpp"
#include "Renderer/Resources/ImageView.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"

// Resolution and grid scale mirror TerrainManager's VerticesPerEdge/ChunkScale (used only to
// feed the terrain shaders' specialization constants) - HeightScale has no other home despite
// TerrainManager also reading it directly (for frustum-culling AABBs), consistent with this
// codebase's existing Game/Renderer cross-layer reads (e.g. TerrainPass.cpp already reads
// TerrainManager's globals the same way, just in the other direction).
struct TerrainPassConfig {
    f32 HeightScale = 210.0f;
};

class TerrainPass : public Pass {
public:
    TerrainPassConfig Config = {};

    IncResult Init() override;
    void Destroy() override;
    void Render() override;
    void FrameSensibleTransfers() override;

    struct HeightmapResource {
        static constexpr VkFormat Format = VK_FORMAT_R16_UNORM;

        ImageId Image;
        ImageViewId View;
        VkSampler Sampler = VK_NULL_HANDLE;

        // Queues an upload of one heightmap layer, without submitting - callers decide when to
        // flush (GTransferPipe.LazySubmit() for a startup batch, .SubmitReleaseAndWrite() for one
        // streamed-in chunk). Keeps TerrainManager from needing to know this pass's internal
        // Image handle or which TransferPipe function queues an image slice.
        Ticket QueueSlice(u32 target_layer, const void* data, u64 size);
    };
    HeightmapResource Heightmap;

private:
    struct PlaneMeshResource {
        // Only PlaneMesh cares about the index buffer's shape - nothing outside this file
        // references these.
        static constexpr u32 IndexCount = (TerrainManager::VerticesPerEdge - 1) * (TerrainManager::VerticesPerEdge - 1) * 6;
        static constexpr u32 IndexBufferSize = IndexCount * sizeof(u32);

        BufferId Indices;

        void GenerateIndices(u32* indices_begin);
        IncResult Upload();
    };
    PlaneMeshResource PlaneMesh;

    std::array<VkDescriptorSet, RendererConstants::MAX_FRAMES_IN_FLIGHT> DescriptorSets = { VK_NULL_HANDLE };
    std::array<BufferId, RendererConstants::MAX_FRAMES_IN_FLIGHT> ChunkDrawListBuffers;

    VkPipeline TerrainPipeline {};
    VkPipelineLayout TerrainPipelineLayout {};

    void OutTerrainData();
};
