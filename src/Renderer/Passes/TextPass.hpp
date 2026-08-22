#pragma once

#include <array>

#include <vulkan/vulkan.h>

#include "Pass.hpp"
#include "Core/Math.hpp"
#include "Renderer/RendererConstants.hpp"
#include "Renderer/Resources/Buffer.hpp"

// Screen-space 2D text, built from quads via the vendored stb_easy_font (libs/stb/stb_easy_font.h -
// no font asset needed, ASCII-only bitmap font baked into the header itself). General-purpose
// engine capability, not tied to any one subsystem's debug output - ImGui stays the debug-only UI;
// this is the foundation for any future non-ImGui, in-world or HUD text.
class TextPass : public Pass {
public:
    IncResult Init() override;
    void Destroy() override;
    void Render() override;
    void FrameSensibleTransfers() override;

    // Queues one line of text (can contain '\n') for this frame's draw, in screen-space pixel
    // coordinates (origin top-left, x right, y down - matches stb_easy_font's own convention and
    // this engine's Vulkan NDC, so no axis flip is needed anywhere in the pipeline). Accumulates
    // into a CPU buffer that FrameSensibleTransfers() uploads once per frame, then clears - safe
    // to call from anywhere in the frame, including Game-side code running after Engine::Frame()
    // returns (that text lands on the *next* frame, same one-frame latency
    // TerrainManager::ChunkDrawList already has relative to TerrainPass).
    void DrawText(const char* text, f32 x, f32 y, vec3 color, f32 scale = 1.0f);

private:
    // ~270 bytes/character per stb_easy_font's own docs at 4 verts/quad, 16 bytes/vert -> a
    // generous fixed capacity for debug-scale text (a few hundred characters/frame), not meant for
    // large HUD text walls.
    static constexpr u32 MaxQuads = 4096;
    static constexpr u32 MaxVertices = MaxQuads * 4;
    static constexpr u32 MaxIndices = MaxQuads * 6;

    // Matches stb_easy_font_print()'s documented interleaved output layout exactly (x,y,z:float,
    // color:uint8[4]) - passed its output buffer directly, no conversion step.
    struct TextVertex {
        f32 X, Y, Z;
        u8 Color[4];
    };

    std::array<TextVertex, MaxVertices> StagingVertices;
    u32 StagingVertexCount = 0;

    std::array<BufferId, RendererConstants::MAX_FRAMES_IN_FLIGHT> VertexBuffers;
    BufferId IndexBuffer;

    VkPipeline TextPipeline {};
    VkPipelineLayout TextPipelineLayout {};

    void OutTextData();
};
