#pragma once

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "Pass.hpp"
#include "Renderer/RendererConstants.hpp"
#include "Renderer/Resources/Buffer.hpp"
#include "Renderer/Resources/Image.hpp"
#include "Renderer/Resources/ImageView.hpp"
#include "Game/TerrainManager/TerrainManager.hpp"

// Instanced prop drawing (trees, rocks, ...) scattered across terrain chunks. Shaped like
// TerrainPass (Init/Destroy/Render/FrameSensibleTransfers), one instanced draw call per model
// type, reading placement data TerrainManager already generated alongside each chunk's heightmap.
class PropPass : public Pass {
public:
    // Instanced is the normal path. PerInstance issues one vkCmdDrawIndexed per instance (same
    // SSBO, firstInstance = i, so prop.vert's gl_InstanceIndex lookup is unchanged) - the
    // non-instanced baseline the benchmark compares against. Off skips props entirely.
    enum class DrawMode : u8 { Instanced, PerInstance, Off };
    DrawMode Mode = DrawMode::Instanced;

    // Props past this horizontal distance from the camera aren't drawn at all. Without it every
    // ring's chunks contribute props out to the full coverage radius - tens of thousands of
    // sub-pixel instances, which real (thousands-of-triangles) art can't afford.
    f32 DrawDistance = 1200.0f;

    u32 DrawCallsLastFrame = 0;
    u32 InstancesLastFrame = 0;

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
        vec4 ScaleAndColor;        // x = scale, yzw = per-instance linear tint multiplier (color variation)
    };

    // Fragment-stage push constant, one per SubMesh draw - matches prop.frag's PropPushConstants.
    struct PushConstants {
        glm::vec3 Tint;    // linear albedo multiplier (.mtl Kd, or the model's fallback color)
        u32 LayerAndFlags;  // layer in TextureImage (0 = plain white) | FoliageFlag
    };
    // Set for submeshes whose texture has cut-out alpha (leaf cards): prop.frag lights those as
    // thin translucent foliage instead of as an opaque surface.
    static constexpr u32 FoliageFlag = 1u << 31;
    static_assert(sizeof(PushConstants) == 16);

    struct SubMeshResource {
        u32 FirstIndex = 0;
        u32 IndexCount = 0;
        u32 TextureLayer = 0;
        bool Foliage = false;
        glm::vec3 Tint = glm::vec3(1.0f);
    };

    struct ModelResource {
        BufferId VertexBuffer;
        BufferId IndexBuffer;
        std::vector<SubMeshResource> SubMeshes;
    };
    std::array<ModelResource, ModelCount> Models;

    // Every texture any model's .mtl references, deduplicated by path - index + 1 is its layer in
    // TextureImage (layer 0 is reserved for plain white). Filled by LoadModel(), consumed once by
    // CreateTextures().
    struct TextureSource {
        std::string Diffuse;
        std::string Alpha;  // separate map_d mask, empty = use Diffuse's own alpha channel
    };
    std::vector<TextureSource> TextureSources;

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

    // One sRGB 2D array holding every prop texture, each resampled to TextureSize and given a full
    // mip chain (alpha-coverage-preserving, so cut-out leaves don't thin out with distance).
    // Uploaded once at Init() on the Graphics queue directly rather than through TransferPipe,
    // whose ownership-transfer path only ever handles mip 0.
    static constexpr u32 TextureSize = 1024;
    static constexpr u32 MaxTextureLayers = 64;
    ImageId TextureImage;
    ImageViewId TextureView;
    VkSampler TextureSampler = VK_NULL_HANDLE;

    // base_color (sRGB) is only used by submeshes with no .mtl material at all; target_height
    // rescales the mesh to a world size in metres (see ModelLoader::NormalizeHeight).
    IncResult LoadModel(TerrainManager::PropModel model, const char* path, vec3 base_color, f32 target_height);
    u32 TextureLayerFor(const std::string& diffuse, const std::string& alpha);
    IncResult CreateTextures();
    void OutPropData();
};
