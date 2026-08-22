#version 450

struct SceneGlobals {
    mat4 ViewProjection;
    vec3 CameraPosition;
    mat4 InverseViewProjection;
    vec3 SunDirection;
};

layout(set = 0, binding = 0) uniform SceneGlobalsData {
    SceneGlobals data;
} sceneGlobalsData;

struct ChunkDrawData {
    ivec2 WorldPos;
    uint TextureLayer;
    float scale;   // this chunk's LOD ring's ChunkScale
};

layout(set = 1, binding = 1) uniform sampler2DArray heightmapSampler;

layout(std430, set = 1, binding = 0) readonly buffer ChunkBuffer {
    ChunkDrawData chunks[];
} chunkLinkDataBuffer;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec3 debugColor;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outWorldPos;

// Mock data, should be filled by using specialization
layout(constant_id = 0) const int RESOLUTION = 64;
// constant_id 1 deliberately unused - GRID_SCALE moved to a per-instance field (currentChunk.scale)
// since chunk world-size now varies per LOD ring.
layout(constant_id = 2) const float HEIGHT_SCALE = 210;

void main() {
    ChunkDrawData currentChunk = chunkLinkDataBuffer.chunks[gl_InstanceIndex];

    float chunkOffsetX = float(currentChunk.WorldPos.x) * currentChunk.scale;
    float chunkOffsetZ = float(currentChunk.WorldPos.y) * currentChunk.scale;

    int xIndex = gl_VertexIndex % RESOLUTION;
    int zIndex = gl_VertexIndex / RESOLUTION;

    float u = float(xIndex) / float(RESOLUTION - 1);
    float v = float(zIndex) / float(RESOLUTION - 1);
    texCoord = vec2(u, v);

    float localX = u * currentChunk.scale;
    float localZ = v * currentChunk.scale;

    float height = texture(heightmapSampler, vec3(u, v, float(currentChunk.TextureLayer))).r;
    vec3 finalWorldPos = vec3(localZ + chunkOffsetX, height * HEIGHT_SCALE, localX + chunkOffsetZ);

    gl_Position = sceneGlobalsData.data.ViewProjection * vec4(finalWorldPos, 1.0);
    outWorldPos = finalWorldPos;

    // Surface normal via finite difference against neighboring heightmap texels - one texel step
    // in each grid direction, the same grid the mesh itself is built from. Tangent directions
    // mirror finalWorldPos's own u/v -> world.x/world.z mapping exactly (not re-derived
    // independently), so the normal stays consistent with the actual rendered surface regardless
    // of which axis u/v happen to be named after.
    float texelStep = 1.0 / float(RESOLUTION - 1);
    float heightU = texture(heightmapSampler, vec3(min(u + texelStep, 1.0), v, float(currentChunk.TextureLayer))).r;
    float heightV = texture(heightmapSampler, vec3(u, min(v + texelStep, 1.0), float(currentChunk.TextureLayer))).r;

    float worldStep = currentChunk.scale * texelStep;
    vec3 tangentU = vec3(0.0, (heightU - height) * HEIGHT_SCALE, worldStep);
    vec3 tangentV = vec3(worldStep, (heightV - height) * HEIGHT_SCALE, 0.0);
    outNormal = normalize(cross(tangentU, tangentV));

    bool checker = ((currentChunk.WorldPos.x + currentChunk.WorldPos.y) % 2) == 0;
    debugColor = checker ? vec3(0.8, 0.2, 0.2) : vec3(0.2, 0.2, 0.8);
}
