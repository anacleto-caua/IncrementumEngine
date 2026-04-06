#include "TerrainSystem.hpp"

#include <array>
#include <cmath>
#include <random>
#include <cstdint>

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <FastNoiseLite.hpp>

#include "Engine/Systems/Terrain/TerrainConfig.hpp"
#include "Engine/InferusRenderer/TransferSystem.hpp"

namespace TerrainSystem {

    // Test only random numbers
    std::random_device rng_device;
    std::mt19937 generator(rng_device());
    std::uniform_int_distribution<> random_chunk_index(0, TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT-1);

    uint16_t* HeightmapsBuffer_MappedMem;
    ChunkHeightmapLink* ChunkLinksBuffer_MappedMem;

    std::array<ChunkHeightmapLink, TerrainConfig::ChunkToHeightmapLinking::INSTANCE_COUNT> ChunkLinksMirror;
    BufferSystem::Id ChunkLinkGpu;
    ImageSystem::Id HeightmapGpu;

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

    void UpdateChunkLinkN(size_t arr_pos);

    void Update() {
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

        // Transfer system testing
        auto idx = random_chunk_index(generator);
        auto& chunk = ChunkLinksMirror[idx];
        if (chunk.Flags == ChunkFlags::None) {
            chunk.Flags = ChunkFlags::Active | ChunkFlags::Visible;
        } else {
            chunk.Flags = ChunkFlags::None;
        }
        UpdateChunkLinkN(idx);
    }

    void UpdateChunkLinkN(size_t arr_pos) {
        TransferSystem::QueueBufferUpdate(
            ChunkLinkGpu,
            &ChunkLinksMirror[arr_pos],
            sizeof(ChunkHeightmapLink),
            sizeof(ChunkHeightmapLink)*arr_pos,
            [arr_pos](){
                spdlog::info("updated {}", arr_pos);
            }
        );
    }

    void WriteChunk(glm::ivec2 ChunkPos, uint16_t* ChunkBegin);

    void FeedTerrainData(BufferSystem::Id ChunkLinkId, ImageSystem::Id HeightmapId) {
        ChunkLinkGpu = ChunkLinkId;
        HeightmapGpu = HeightmapId;

        // Fill mirror array
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
                    ChunkLinksMirror[coords_counter] = {
                        .WorldPos = { x, y },
                        .InstanceId = (coords_counter),
                        .Flags = ChunkFlags::Active | ChunkFlags::Visible
                    };
                    coords_counter++;
                }
            }
        }
    }

    void FeedTerrainRenderer(ChunkHeightmapLink* ChunkLinkMap, uint16_t* HeightmapMap) {
        ChunkLinksBuffer_MappedMem = ChunkLinkMap;
        HeightmapsBuffer_MappedMem = HeightmapMap;
        FullWriteChunkData(); // TODO: hacky
    }


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
                        .Flags = ChunkFlags::Active | ChunkFlags::Visible
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
