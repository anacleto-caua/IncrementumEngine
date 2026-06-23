#pragma once

#include <vulkan/vulkan.h> // Used for the HeightmapFormat
#include <glm/ext/vector_int2.hpp>

/**
 * A lot of variables here should match something at the terrain shaders
 * so keep and eye out for it since there's no reflection.
 */
namespace TerrainConfig {

    // Defines the structural geometry of a single chunk
    namespace Mesh {
        constexpr u32 VerticesPerEdge = 64;
        constexpr f64 ChunkScale = 50.0;

        constexpr f64 HeightScale = []() {
            // Newton-Raphson method for approximating sqrt comptime
            f64 pseudo_height_scale = 30.0;
            f64 x = ChunkScale;
            f64 prev = 0.0;
            while (x != prev) {
                prev = x;
                x = 0.5 * (x + ChunkScale / x);
            }
            return pseudo_height_scale * x;
        }();

        constexpr u32 IndexCount = (VerticesPerEdge - 1) * (VerticesPerEdge - 1) * 6;
        constexpr u32 IndexBufferSize = IndexCount * sizeof(u32);
    };

    // Defines how the world loads around the player
    namespace Streaming {
        constexpr u32 ExplorationRadius = 8;

        // The maximum number of chunks that can be loaded in memory at once
        constexpr u32 MaxActiveChunks = []() {
            u32 quadrant_points = 0;
            u32 radius = ExplorationRadius;
            u32 y = radius;

            for (u32 x = 1; x <= radius; x++) {
                while (x * x + y * y > radius * radius) {
                    y--;
                }
                quadrant_points += y;
            }
            return 1 + 4 * radius + 4 * quadrant_points;
        }();
    };

    // Defines the sizes for the Vulkan Allocations used by the terrain system
    namespace Memory {
        // Heightmap Texture Array
        constexpr VkFormat HeightmapFormat = VK_FORMAT_R16_UNORM;
        constexpr u64 PixelsPerLayer = Mesh::VerticesPerEdge * Mesh::VerticesPerEdge;
        constexpr u64 LayerSizeBytes = PixelsPerLayer * sizeof(u16);

        // The actual size of the global Texture2DArray
        constexpr u64 TotalTextureArraySize = LayerSizeBytes * Streaming::MaxActiveChunks;

        // Instance struct to assembly render batchs for terrain chunks
        struct ChunkInstanceData {
            glm::ivec2 WorldPos;
            u32 TextureLayer;
            u32 padding;
        };

        // The maximum size of the SSBO you upload to the GPU every frame
        constexpr u64 MaxInstanceBufferSize = Streaming::MaxActiveChunks * sizeof(ChunkInstanceData);
    };
};
