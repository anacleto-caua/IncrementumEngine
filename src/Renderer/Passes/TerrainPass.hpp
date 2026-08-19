#pragma once

#include <glm/fwd.hpp>

#include <vulkan/vulkan.h>

#include "Renderer/Vk/TimelineSemaphore.hpp"

// Resolution and grid scale mirror TerrainManager's VerticesPerEdge/ChunkScale (used only to
// feed the terrain shaders' specialization constants) - HeightScale has no other home, since
// nothing outside rendering needs to know how normalized heightmap values map to world Y.
struct TerrainPassConfig {
    f32 HeightScale = 210.0f;
};

namespace TerrainPass {
    inline TerrainPassConfig Config = {};

    namespace Heightmap {
        constexpr VkFormat Format = VK_FORMAT_R16_UNORM;

        // Queues an upload of one heightmap layer, without submitting - callers decide when to
        // flush (TransferPipe::LazySubmit() for a startup batch, SubmitReleaseAndWrite() for one
        // streamed-in chunk). Keeps TerrainManager from needing to know this pass's internal
        // Image handle or which TransferPipe function queues an image slice.
        Ticket QueueSlice(u32 target_layer, const void* data, u64 size);
    }

    IncResult Create();
    void Destroy();

    void FrameSensibleTransfers();
    void Render();
}
