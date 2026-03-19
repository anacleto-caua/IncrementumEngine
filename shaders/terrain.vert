#version 450

struct ChunkHeightmapLink {
    ivec2 worldPos;
    uint instanceId;
    uint isVisible;
};

layout(set = 0, binding = 0) uniform sampler2DArray heightmapSampler;

layout(std430, set = 0, binding = 1) readonly buffer ChunkBuffer {
    ChunkHeightmapLink chunks[];
} chunkLinkDataBuffer;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec3 debugColor;

layout(push_constant) uniform PushConstants {
    mat4 lookAt;
    vec3 playerPos;
    float padding;
} terrain_push;

const int RESOLUTION = 64;
const float GRID_SCALE = 50.0;
const float HEIGHT_SCALE = 30.0 * sqrt(GRID_SCALE); // Still doesn't feel like it

void main() {
    ChunkHeightmapLink currentChunk = chunkLinkDataBuffer.chunks[gl_InstanceIndex];

    if (currentChunk.isVisible == 0) {
        gl_Position = vec4(0.0/0.0);
        return;
    }

    float chunkOffsetX = float(currentChunk.worldPos.x) * GRID_SCALE;
    float chunkOffsetZ = float(currentChunk.worldPos.y) * GRID_SCALE;

    int xIndex = gl_VertexIndex % RESOLUTION;
    int zIndex = gl_VertexIndex / RESOLUTION;

    float u = float(xIndex) / float(RESOLUTION - 1);
    float v = float(zIndex) / float(RESOLUTION - 1);
    texCoord = vec2(u, v);

    float localX = u * GRID_SCALE;
    float localZ = v * GRID_SCALE;

    float height = texture(heightmapSampler, vec3(u, v, float(gl_InstanceIndex))).r;
    vec3 finalWorldPos = vec3(localZ + chunkOffsetX, height * HEIGHT_SCALE, localX + chunkOffsetZ);

    gl_Position = terrain_push.lookAt * vec4(finalWorldPos, 1.0);

    bool checker = ((currentChunk.worldPos.x + currentChunk.worldPos.y) % 2) == 0;
    debugColor = checker ? vec3(0.8, 0.2, 0.2) : vec3(0.2, 0.2, 0.8);
}
