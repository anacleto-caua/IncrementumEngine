#include "TerrainManager.hpp"

#include <array>

#include <glm/glm.hpp>
#include <FastNoiseLite.hpp>

#include "TerrainDefinitions.hpp"

namespace TerrainManager {
    void WriteHeightmap(glm::ivec2 position, u32 target_layer);
    void FullWriteChunkData();

    using Heightmap = u16[TerrainConfig::Mesh::VerticesPerEdge][TerrainConfig::Mesh::VerticesPerEdge];

    struct HeightmapStatus {
        glm::ivec2 Position = { 0, 0 };
        bool Ready = false;
    };

    std::array<TerrainConfig::Memory::ChunkInstanceData, TerrainConfig::Streaming::MaxActiveChunks> ChunkDrawList;
    std::array<HeightmapStatus, TerrainConfig::Streaming::MaxActiveChunks> HeightmapStatus;
    std::array<Heightmap, TerrainConfig::Streaming::MaxActiveChunks> HeightmapData;

    FastNoiseLite ContinentalNoise;
    FastNoiseLite MountainNoise;
    FastNoiseLite DetailNoise;

    void Init() {
        ContinentalNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ContinentalNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        ContinentalNoise.SetFractalOctaves(5);
        ContinentalNoise.SetFrequency(0.002f);

        MountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        MountainNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        MountainNoise.SetFractalOctaves(6);
        MountainNoise.SetFrequency(0.015f);

        DetailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        DetailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        DetailNoise.SetFractalOctaves(4);
        DetailNoise.SetFrequency(0.08f);
    }

    void RefreshChunks(glm::ivec2 current_player_chunk) {
        // ...
    }

    void FullWriteChunkData() {
        glm::vec3 player_pos = {0, 0, 0};
        glm::ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_pos.x/TerrainConfig::Mesh::ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_pos.y/TerrainConfig::Mesh::ChunkScale));

        u32 coords_counter = 0;
        i32 radius = TerrainConfig::Streaming::ExplorationRadius;
        i32 r_squared = radius*radius;

        // Circle around the player
        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {

                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;

                // Valid point
                if ((dx * dx) + (dy * dy) <= r_squared) {
                    ChunkDrawList[coords_counter] = {
                        .WorldPos = { x, y },
                        .TextureLayer = 0
                    };
                    coords_counter++;

                    WriteHeightmap({x, y}, coords_counter);
                }
            }
        }
    }

    void WriteHeightmap(glm::ivec2 position, u32 target_layer) {
        i32 terrain_res = TerrainConfig::Mesh::VerticesPerEdge;

        f32 global_x, global_z;
        for (i32 x = 0; x < terrain_res; x++) {
            global_x = static_cast<f32>(x + ((terrain_res-1) * position.x));
            for (i32 z = 0; z < terrain_res; z++) {
                global_z = static_cast<f32>(z + ((terrain_res-1) * position.y));

                f32 cont = (ContinentalNoise.GetNoise(global_x, global_z) + 1.0f) * 0.5f;
                f32 mount = (MountainNoise.GetNoise(global_x, global_z) + 1.0f) * 0.5f;
                f32 detail = (DetailNoise.GetNoise(global_x, global_z) + 1.0f) * 0.5f;

                cont = std::pow(cont, 1.5f);

                f32 mountain_mask = 0.0f;
                if (cont > 0.45f) {
                    mountain_mask = (cont - 0.45f) / 0.55f;
                    mountain_mask = mountain_mask * mountain_mask * (3.0f - 2.0f * mountain_mask);
                }

                f32 detail_mask = .05f;
                if (mountain_mask <= 0.5f) {
                    detail_mask = 3.0f * ((.05f - mountain_mask)*.05f);
                }

                f32 elevation = (cont * 0.30f) + (mount * mountain_mask * 0.65f) + (detail * detail_mask);

                if (elevation <= 0.0) {
                    elevation = 0.0;
                } else if (elevation >= 1.0) {
                    elevation = 1.0;
                }

                f32 remapped = elevation * 65535.0f;
                u16 end_value = static_cast<u16>(remapped);
                HeightmapData[target_layer][x][z] = end_value;
            }
        }
    }
}
