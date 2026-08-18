#include "TerrainManager.hpp"

#include <FastNoiseLite.hpp>

#include "Renderer/Passes/TerrainPass.hpp"
#include "Renderer/Resources/TransferPipe.hpp"

namespace TerrainManager {
    void WriteHeightmap(ivec2 position, u32 target_layer);

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

        // Kickstart the valid data
        vec3 player_pos = {0, 0, 0};
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_pos.x/TerrainConfig::Mesh::ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_pos.z/TerrainConfig::Mesh::ChunkScale));

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
                        .TextureLayer = coords_counter,
                        .padding = 0
                    };
                    HeightmapStatus[coords_counter] = {
                        .Position = { x, y },
                        .Ready = true
                    };

                    WriteHeightmap({x, y}, coords_counter);

                    TransferPipe::QueueImageSliceUpload(
                        TerrainPass::Heightmap::Image,
                        coords_counter,
                        &HeightmapData[coords_counter],
                        sizeof(Heightmap)
                    );

                    coords_counter++;
                }
            }
        }

        TransferPipe::LazySubmit();
    }

    void RefreshChunks(vec3 player_position) {
        ivec2 player_coord;
        player_coord.x = static_cast<i32>(std::floor(player_position.x / TerrainConfig::Mesh::ChunkScale));
        player_coord.y = static_cast<i32>(std::floor(player_position.z / TerrainConfig::Mesh::ChunkScale));

        constexpr i32 radius = TerrainConfig::Streaming::ExplorationRadius;
        constexpr i32 r_squared = radius * radius;

        u32 replacements_done = 0;

        for (i32 x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            if (replacements_done >= TerrainConfig::Streaming::MaxChunkReplacementsPerFrame) { break; }

            for (i32 y = player_coord.y - radius; y <= player_coord.y + radius; y++) {
                if (replacements_done >= TerrainConfig::Streaming::MaxChunkReplacementsPerFrame) { break; }

                i32 dx = x - player_coord.x;
                i32 dy = y - player_coord.y;
                if ((dx * dx) + (dy * dy) > r_squared) { continue; }

                ivec2 candidate = { x, y };

                // Already loaded into some active slot - nothing to do
                bool already_loaded = false;
                for (u32 i = 0; i < TerrainConfig::Streaming::MaxActiveChunks; i++) {
                    if (HeightmapStatus[i].Position == candidate) {
                        already_loaded = true;
                        break;
                    }
                }
                if (already_loaded) { continue; }

                // Find a slot whose current chunk has fallen out of range to evict
                u32 slot_to_replace = UINT32_MAX;
                for (u32 i = 0; i < TerrainConfig::Streaming::MaxActiveChunks; i++) {
                    ivec2 held = HeightmapStatus[i].Position;
                    i32 hdx = held.x - player_coord.x;
                    i32 hdy = held.y - player_coord.y;
                    if ((hdx * hdx) + (hdy * hdy) > r_squared) {
                        slot_to_replace = i;
                        break;
                    }
                }
                if (slot_to_replace == UINT32_MAX) { continue; }

                ChunkDrawList[slot_to_replace] = {
                    .WorldPos = candidate,
                    .TextureLayer = slot_to_replace,
                    .padding = 0
                };
                HeightmapStatus[slot_to_replace] = {
                    .Position = candidate,
                    .Ready = false
                };

                WriteHeightmap(candidate, slot_to_replace);

                TransferPipe::QueueImageSliceUpload(
                    TerrainPass::Heightmap::Image,
                    slot_to_replace,
                    &HeightmapData[slot_to_replace],
                    sizeof(Heightmap)
                );

                HeightmapStatus[slot_to_replace].Ready = true;

                replacements_done++;
            }
        }

        if (replacements_done > 0) {
            TransferPipe::SubmitReleaseAndWrite();
        }
    }

    void WriteHeightmap(ivec2 position, u32 target_layer) {
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
