#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "Pass.hpp"
#include "Renderer/RendererConstants.hpp"
#include "Renderer/Resources/Buffer.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"

// Instanced prop drawing (trees, rocks, ...) scattered across terrain chunks. Shaped like
// TerrainPass (Init/Destroy/Render/FrameSensibleTransfers), one instanced draw call per model
// type, reading placement data TerrainManager already generated alongside each chunk's heightmap.
class PropPass : public Pass {
public:
    IncResult Init() override;
    void Destroy() override;
    void Render() override;
    void FrameSensibleTransfers() override;

private:
    static constexpr u32 ModelCount = static_cast<u32>(TerrainManager::PropModel::_COUNT_);

    // GPU instance layout - matches shaders/prop.vert's PropInstance struct exactly. vec4 pairs
    // sidestep std430's vec3-alignment gotcha entirely (Core/Math.hpp documents the same class of
    // footgun for a derived-vec3-in-a-shared-layout situation) rather than hand-computing padding.
    struct GpuInstance {
        vec4 PositionAndRotation;  // xyz = world position, w = Y rotation (radians)
        vec4 ScaleAndColor;        // x = scale, yzw = flat base color (RGB, no textures in v1)
    };

    struct ModelResource {
        BufferId VertexBuffer;
        BufferId IndexBuffer;
        u32 IndexCount = 0;
        vec3 BaseColor;
    };
    std::array<ModelResource, ModelCount> Models;

    // Worst case: every currently-drawn chunk's props all happen to be this one model type.
    // Wasteful in the common case (models are usually mixed) but bounded and simple - matches this
    // codebase's existing "conservative fixed-capacity sizing" convention (e.g. TerrainManager's
    // own cache arrays) over a resizable/dynamic buffer.
    static constexpr u32 MaxInstancesPerModel = TerrainManager::TotalMaxDrawnChunks * TerrainManager::MaxPropsPerChunk;

    std::array<std::array<BufferId, RendererConstants::MAX_FRAMES_IN_FLIGHT>, ModelCount> InstanceBuffers;
    std::array<std::array<VkDescriptorSet, RendererConstants::MAX_FRAMES_IN_FLIGHT>, ModelCount> DescriptorSets;

    // CPU-side staging, rebuilt every frame in FrameSensibleTransfers() then pushed to the current
    // frame-in-flight's buffer - std::vector, but reserved once at Init() to MaxInstancesPerModel
    // and only ever cleared/push_back'd within that reserved capacity, so there's no per-frame
    // heap allocation despite the dynamic-looking type.
    std::array<std::vector<GpuInstance>, ModelCount> StagingInstances;

    VkPipeline PropPipeline {};
    VkPipelineLayout PropPipelineLayout {};

    IncResult LoadModel(TerrainManager::PropModel model, const char* path, vec3 base_color);
    void OutPropData();
};
