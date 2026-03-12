#include "TerrainSystem.hpp"

#include <cstdint>
#include <imgui.h>

#include "Engine/Systems/Terrain/TerrainConfig.hpp"

namespace TerrainSystem {

    uint16_t* HeightmapsBuffer_MappedMem;
    ChunkHeightmapLink* ChunkLinksBuffer_MappedMem;

    glm::vec3* PlayerPos;

    FastNoiseLite BaseNoise;

    void Create(glm::vec3* pPlayerPos) {
        PlayerPos = pPlayerPos;

        BaseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        BaseNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        BaseNoise.SetFractalOctaves(8);
        BaseNoise.SetFrequency(.02);
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

        uint32_t x = static_cast<uint32_t>(PlayerPos->x/TerrainConfig::Chunk::RESOLUTION);
        uint32_t z = static_cast<uint32_t>(PlayerPos->z/TerrainConfig::Chunk::RESOLUTION);

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
        // Diamond scan the area around the player
        glm::ivec2 player_coord;
        player_coord.x = PlayerPos->x/TerrainConfig::Chunk::RESOLUTION;
        player_coord.y = PlayerPos->z/TerrainConfig::Chunk::RESOLUTION;

        uint32_t coords_counter = TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT - 1;    // The last array position
        // Add the player position as it's the last chunk that should be drawn
        ChunkLinksBuffer_MappedMem[coords_counter] = {
            .WorldPos = player_coord,
            .InstanceId = (uint32_t)coords_counter,
            .IsVisible = 1
        };
        coords_counter--;

        for (uint32_t i = 0; i < TerrainConfig::ChunkToHeightmapLinking::DIAMOND_EXPLORATION_RADIUS; i++) {
            int32_t x_pos = player_coord.x + i + 1;
            int32_t x_neg = player_coord.x - i + 1;
            for (uint32_t j = 0; j < (TerrainConfig::ChunkToHeightmapLinking::DIAMOND_EXPLORATION_RADIUS - i); j++) {
                int32_t y_pos = player_coord.y + j;
                int32_t y_neg = player_coord.y - j;

                // Memory Layout: [Link3][Link2][Link1][Link0]
                ChunkHeightmapLink* block = &ChunkLinksBuffer_MappedMem[coords_counter - 3];

                // We write sequentially to the memory block (0, 1, 2, 3).
                block[0] = {
                    .WorldPos = { x_neg, y_pos },
                    .InstanceId = (coords_counter - 3),
                    .IsVisible = 1
                };
                block[1] = {
                    .WorldPos = { x_pos, y_neg },
                    .InstanceId = (coords_counter - 2),
                    .IsVisible = 1
                };
                block[2] = {
                    .WorldPos = { x_neg, y_neg },
                    .InstanceId = (coords_counter - 1),
                    .IsVisible = 1
                };
                block[3] = {
                    .WorldPos = { x_pos, y_pos },
                    .InstanceId = (coords_counter),
                    .IsVisible = 1
                };

                coords_counter -= 4;
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

                float n = BaseNoise.GetNoise(globalX, globalZ);
                float remapped = (n + 1.0f) * 0.5f * 65535.0f;

                *ChunkBegin++ = static_cast<uint16_t>(remapped);
            }
        }
    }
}
