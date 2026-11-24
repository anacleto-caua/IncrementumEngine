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
    // World units at normalized height 1.0. Tall enough for real mountain ranges to read against
    // a ~16000-unit view (210 made everything look like rolling hills); R16 heightmaps still
    // resolve ~0.014 units per step at this scale.
    f32 HeightScale = 900.0f;
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

    // ImGui-toggleable, pushed to terrain.frag every frame (a push constant rather than a
    // specialization constant specifically so it can flip without a pipeline rebuild) - swaps
    // between the procedural grass/rock/snow terrain texture (default) and the flat per-chunk
    // checker pattern that's been this codebase's debug view since before texturing existed,
    // still useful for chunk-boundary/LOD debugging. Public for Game/Screenshots.
    bool ShowChunkDebugColors = false;

    // LOD ring visualization (Game/Screenshots' lod shot set), all pushed in the same flags word
    // as ShowChunkDebugColors:
    //   ShowRingColors     albedo = RingColors[ring] instead of the procedural material (water too)
    //   ShowChunkOutlines  thin dark line along every chunk's border
    //   ShowWireframe      the mesh's triangle edges, drawn in the fragment shader from texCoord -
    //                      no VK_POLYGON_MODE_LINE (needs fillModeNonSolid, and would lose the
    //                      shaded surface under the lines)
    //   ShowCameraMarker   dot at the camera's XZ, for the top-down view
    //   FogEnabled         off for the top-down view, where every pixel is ~equally far away
    bool ShowRingColors = false;
    bool ShowChunkOutlines = false;
    bool ShowWireframe = false;
    bool ShowCameraMarker = false;
    bool FogEnabled = true;

    // sRGB 0xRRGGBB per ring, ring 0 first - five viridis samples evenly spaced in CIE L*
    // (24.5, 42.0, 58.6, 74.6, 90.9), so the bands also read in grayscale print. Fed to
    // terrain.frag as specialization constants, so this is the one place to change them.
    static constexpr std::array<u32, TerrainManager::OuterRingCount + 1> RingColors = {
        0x482878, 0x31688E, 0x1F9E89, 0x6DCD59, 0xFDE725
    };

private:
    // Bits of terrain.frag's TerrainPushConstants::flags.
    enum PushFlags : u32 {
        Flag_ChunkDebugColors = 1u << 0,
        Flag_RingColors       = 1u << 1,
        Flag_ChunkOutlines    = 1u << 2,
        Flag_Wireframe        = 1u << 3,
        Flag_NoFog            = 1u << 4,
        Flag_CameraMarker     = 1u << 5,
    };

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
