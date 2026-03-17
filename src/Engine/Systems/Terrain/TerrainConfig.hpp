#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "Engine/Systems/Terrain/TerrainTypes.hpp"

namespace TerrainConfig {
    namespace Chunk {
        constexpr uint32_t RESOLUTION = 64;

        constexpr uint32_t INDICES_COUNT = (RESOLUTION - 1) * (RESOLUTION - 1) * 6;

        constexpr uint32_t INDICES_BUFFER_SIZE = INDICES_COUNT * sizeof(uint32_t);
    };

    namespace ChunkToHeightmapLinking {
        constexpr uint32_t EXPLORATION_RADIUS = 8;

        constexpr uint32_t INSTANCE_COUNT = []() {
            uint32_t quadrant_points = 0;
            uint32_t radius = EXPLORATION_RADIUS;
            uint32_t y = radius;

            for (uint32_t x = 1; x <= radius; x++) {
                while (x * x + y * y > radius * radius) {
                    y--;
                }
                quadrant_points += y;
            }
            return 1 + 4 * radius + 4 * quadrant_points;
        }();

        constexpr uint32_t LINKING_BUFFER_SIZE = INSTANCE_COUNT * sizeof(ChunkHeightmapLink);
    };

    namespace Heightmap {
        constexpr VkFormat HEIGHTMAP_IMAGE_FORMAT = VK_FORMAT_R16_UNORM;

        constexpr size_t HEIGHTMAP_IMAGE_PIXEL_COUNT = TerrainConfig::Chunk::RESOLUTION * TerrainConfig::Chunk::RESOLUTION;

        constexpr size_t HEIGHTMAP_ALL_IMAGES_PIXEL_COUNT = HEIGHTMAP_IMAGE_PIXEL_COUNT * TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT;

        constexpr size_t HEIGHTMAP_IMAGE_SIZE =  HEIGHTMAP_IMAGE_PIXEL_COUNT * sizeof(uint16_t);

        constexpr size_t HEIGHTMAP_ALL_IMAGES_SIZE =  HEIGHTMAP_ALL_IMAGES_PIXEL_COUNT * sizeof(uint16_t);
    };
};
