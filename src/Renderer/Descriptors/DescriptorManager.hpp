#pragma once

#include <vulkan/vulkan.h>

namespace DescriptorManager {

    // Globally exposed Layouts
    inline VkDescriptorSetLayout GlobalLayout = VK_NULL_HANDLE;
    inline VkDescriptorSetLayout PerFrameLayout = VK_NULL_HANDLE;      // terrain's own Set 1
    inline VkDescriptorSetLayout PropPerFrameLayout = VK_NULL_HANDLE;  // props' own Set 1 - see
    // DescriptorMap::PropPerFrame below for why this is a second, separate layout object rather
    // than more bindings added to PerFrameLayout above.
    inline VkDescriptorSetLayout ComputeTestLayout = VK_NULL_HANDLE;   // ComputeTestDemo's own
    // standalone Set 0 - a pure-compute pipeline has no graphics-frame Set 0 to share.

    IncResult Create();
    void Destroy();

    // Utility to allocate a single set
    VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);
    // Utility to allocate multiple sets (e.g., for Frames In Flight)
    void AllocateSets(VkDescriptorSetLayout layout, u32 count, VkDescriptorSet* sets);
}

/**
 * This section of the file is responsible for tracking the currently used drescriptor set and binding indexes
 * thoose are static and editing them will require manual changes on the shaders to avoid conflict.
 */
namespace DescriptorMap {
    // ================================================
    // SET 0: Global & Static Data (Rarely changes)
    // ================================================
    namespace Global {
        inline constexpr u32 SetIndex = 0;

        inline constexpr u32 Binding_SceneGlobals = 0;
    }

    // ================================================
    // SET 1: Per-Frame-Bound Data (rewritten every frame, or streamed in at a lower frequency -
    // grouped by "how often this changes" rather than a strict "every single frame" guarantee)
    // ================================================
    namespace PerFrame {
        inline constexpr u32 SetIndex = 1;

        inline constexpr u32 Binding_ChunkDrawListSSBO = 0;  // rewritten every frame
        inline constexpr u32 Binding_HeightmapTexture = 1;   // streamed in per chunk, not per frame
    }

    // PropPass's own Set 1 - a second, separate descriptor set layout at the same Vulkan set
    // INDEX (1), not more bindings folded into PerFrame above. Set indices are pipeline-layout-
    // local, so two passes with two different VkPipelineLayouts (TerrainPass's TerrainPipelineLayout
    // vs. PropPass's PropPipelineLayout) can each have their own "set 1" contents - terrain's Set 1
    // set would otherwise need a valid entry for a binding it has no data for, or vice versa.
    namespace PropPerFrame {
        inline constexpr u32 SetIndex = 1;

        inline constexpr u32 Binding_InstanceSSBO = 0;  // rewritten every frame, per model type
        inline constexpr u32 Binding_Texture = 1;       // shared across every model, set once at Init()
    }

    // ================================================
    // SET 2: Material/Entity Data (Changes per object)
    // ================================================
    namespace Material {
        inline constexpr u32 SetIndex = 2;
    }

    // ComputeTestDemo's own standalone pipeline layout (Set 0) - a pure-compute pipeline has no
    // graphics-frame Set 0 to share with, so this doesn't reuse Global's SetIndex despite also
    // being index 0 within its own, separate VkPipelineLayout.
    namespace ComputeTest {
        inline constexpr u32 SetIndex = 0;

        inline constexpr u32 Binding_TestBuffer = 0;
    }
}


