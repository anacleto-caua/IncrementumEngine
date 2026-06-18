/**
 * This single file is responsible for tracking the currently used drescriptor set and binding indexes
 * thoose are static and editing them will require manual changes on the shaders to avoid conflict.
 */
#pragma once

#include <vulkan/vulkan.h>

namespace DescriptorMap {
    // ================================================
    // SET 0: Global & Static Data (Rarely changes)
    // ================================================
    namespace Global {
        inline constexpr u32 SetIndex = 0;
    }

    // ================================================
    // SET 1: Per-Frame Data (Changes every frame)
    // ================================================
    namespace PerFrame {
        inline constexpr u32 SetIndex = 1;

        inline constexpr u32 Binding_ChunkDrawListSSBO = 0;
        inline constexpr u32 Binding_HeightmapTexture = 1; // This isn't per frame but okay...
    }

    // ================================================
    // SET 2: Material/Entity Data (Changes per object)
    // ================================================
    namespace Material {
        inline constexpr u32 SetIndex = 2;
    }
}
