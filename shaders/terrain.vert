#version 450

struct SceneGlobals {
    mat4 ViewProjection;
    vec3 CameraPosition;
};

layout(set = 0, binding = 0) uniform SceneGlobalsData {
    SceneGlobals data;
} sceneGlobalsData;

struct ChunkDrawData {
    ivec2 WorldPos;
    uint TextureLayer;
    uint padding;
};

layout(set = 1, binding = 1) uniform sampler2DArray heightmapSampler;

layout(std430, set = 1, binding = 0) readonly buffer ChunkBuffer {
    ChunkDrawData chunks[];
} chunkLinkDataBuffer;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec3 debugColor;

// Mock data, should be filled by using specialization
layout(constant_id = 0) const int RESOLUTION = 64;
layout(constant_id = 1) const float GRID_SCALE = 50.0;
layout(constant_id = 2) const float HEIGHT_SCALE = 210;

void main() {
    ChunkDrawData currentChunk = chunkLinkDataBuffer.chunks[gl_InstanceIndex];

    float chunkOffsetX = float(currentChunk.WorldPos.x) * GRID_SCALE;
    float chunkOffsetZ = float(currentChunk.WorldPos.y) * GRID_SCALE;

    int xIndex = gl_VertexIndex % RESOLUTION;
    int zIndex = gl_VertexIndex / RESOLUTION;

    float u = float(xIndex) / float(RESOLUTION - 1);
    float v = float(zIndex) / float(RESOLUTION - 1);
    texCoord = vec2(u, v);

    float localX = u * GRID_SCALE;
    float localZ = v * GRID_SCALE;

    float height = texture(heightmapSampler, vec3(u, v, float(currentChunk.TextureLayer))).r;
    vec3 finalWorldPos = vec3(localZ + chunkOffsetX, height * HEIGHT_SCALE, localX + chunkOffsetZ);

    gl_Position = sceneGlobalsData.data.ViewProjection * vec4(finalWorldPos, 1.0);

    bool checker = ((currentChunk.WorldPos.x + currentChunk.WorldPos.y) % 2) == 0;
    debugColor = checker ? vec3(0.8, 0.2, 0.2) : vec3(0.2, 0.2, 0.8);
}
