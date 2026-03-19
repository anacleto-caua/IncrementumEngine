#include "TerrainSystem.hpp"

#include <cmath>
#include <cstdint>

#include <imgui.h>
#include <FastNoiseLite.hpp>

#include "Engine/Systems/Terrain/TerrainConfig.hpp"

namespace TerrainSystem {

    uint16_t* HeightmapsBuffer_MappedMem;
    ChunkHeightmapLink* ChunkLinksBuffer_MappedMem;

    glm::vec3* PlayerPos;

    FastNoiseLite ContinentalNoise;
    FastNoiseLite MountainNoise;
    FastNoiseLite DetailNoise;

    void Create(glm::vec3* pPlayerPos) {
        PlayerPos = pPlayerPos;

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

    void Destroy() {
        // ...
    }

    void Update() {
        // TODO: Add some sort of sync system, update chunk after X seconds, after Y distance
        // has been travelled from center...
        // ------------------------------------------------------------------------------------
        // The best would be always be loading and unloading chunks, what I think could be done
        // if the Heightmap and ChunkLinks had some scratch space, given the usage of same size
        // allocations we could just update the other ones and provide a glInstanceIndex offset
        // on the push constants? Maybe have double or triple buffering on Heightmap and
        // ChunkLink information? I think the last option seems better.
        // -- Turns out all that stuff has already been figured out, so I may just pick one or
        // a combination of them and rock with it. One could also consider do a performance
        // comparisson and etc.

        ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Terrain System");

        uint32_t x = static_cast<uint32_t>(std::floor(PlayerPos->x/TerrainConfig::Chunk::CHUNK_SCALE));
        uint32_t z = static_cast<uint32_t>(std::floor(PlayerPos->z/TerrainConfig::Chunk::CHUNK_SCALE));

        ImGui::TextDisabled("Player pos:");
        ImGui::Indent();
        ImGui::Text("x: %05f y: %05f z: %05f", PlayerPos->x, PlayerPos->y, PlayerPos->z);
        ImGui::Unindent();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Current player chunk:");
        ImGui::Indent();
        ImGui::Text("X: %03d Z: %03d", x, z);
        ImGui::Unindent();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Clearly mocked data as of now
        ImGui::TextDisabled("Camera casting:");
        ImGui::Indent();
        ImGui::Text("X: %03d Z: %03d", x, z);
        ImGui::Unindent();

        ImGui::End();
    }

    void FeedTerrainRenderer(ChunkHeightmapLink* ChunkLinkMap, uint16_t* HeightmapMap) {
        ChunkLinksBuffer_MappedMem = ChunkLinkMap;
        HeightmapsBuffer_MappedMem = HeightmapMap;
        FullWriteChunkData(); // TODO: hacky
    }

    void WriteChunk(glm::ivec2 ChunkPos, uint16_t* ChunkBegin);

    void FullWriteChunkData() {
        glm::ivec2 player_coord;
        player_coord.x = static_cast<int32_t>(std::floor(PlayerPos->x/TerrainConfig::Chunk::CHUNK_SCALE));
        player_coord.y = static_cast<int32_t>(std::floor(PlayerPos->z/TerrainConfig::Chunk::CHUNK_SCALE));

        uint32_t coords_counter = 0;
        int32_t radius = TerrainConfig::ChunkToHeightmapLinking::EXPLORATION_RADIUS;
        int32_t r_squared = radius*radius;

        // Circle around the player
        for (int32_t x = player_coord.x - radius; x <= player_coord.x + radius; x++) {
            for (int32_t y = player_coord.y - radius; y <= player_coord.y + radius; y++) {

                int32_t dx = x - player_coord.x;
                int32_t dy = y - player_coord.y;

                // Valid point
                if ((dx * dx) + (dy * dy) <= r_squared) {
                    ChunkLinksBuffer_MappedMem[coords_counter] = {
                        .WorldPos = { x, y },
                        .InstanceId = (coords_counter),
                        .IsVisible = 1
                    };
                    coords_counter++;
                }
            }
        }

        // TODO:
        // Kinda ugly they're on different loops and it's all in the main thread
        for (uint32_t i = 0; i < TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT; i++) {
            ChunkHeightmapLink cl = ChunkLinksBuffer_MappedMem[i];
            WriteChunk(cl.WorldPos, &HeightmapsBuffer_MappedMem[cl.InstanceId * TerrainConfig::Heightmap::HEIGHTMAP_IMAGE_PIXEL_COUNT]);
        }
    }

    void WriteChunk(glm::ivec2 ChunkPos, uint16_t* ChunkBegin) {
        int32_t TerrainRes = TerrainConfig::Chunk::RESOLUTION;

        float globalX, globalZ;
        for (int32_t x = 0; x < TerrainRes; x++) {
            globalX = x + ((TerrainRes-1) * ChunkPos.x);
            for (int32_t z = 0; z < TerrainRes; z++) {
                globalZ = z + ((TerrainRes-1) * ChunkPos.y);

                float cont = (ContinentalNoise.GetNoise(globalX, globalZ) + 1.0f) * 0.5f;
                float mount = (MountainNoise.GetNoise(globalX, globalZ) + 1.0f) * 0.5f;
                float detail = (DetailNoise.GetNoise(globalX, globalZ) + 1.0f) * 0.5f;

                cont = std::pow(cont, 1.5f);

                float mountainMask = 0.0f;
                if (cont > 0.45f) {
                    mountainMask = (cont - 0.45f) / 0.55f;
                    mountainMask = mountainMask * mountainMask * (3.0f - 2.0f * mountainMask);
                }

                float detailMask = .05f;
                if (mountainMask <= 0.5f) {
                    detailMask = 3.0f * ((.05f - mountainMask)*.05f);
                }

                float elevation = (cont * 0.30f) + (mount * mountainMask * 0.65f) + (detail * detailMask);

                if (elevation <= 0.0) {
                    elevation = 0.0;
                } else if (elevation >= 1.0) {
                    elevation = 1.0;
                }

                float remapped = elevation * 65535.0f;
                *ChunkBegin++ = static_cast<uint16_t>(remapped);
            }
        }
    }
}
