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
layout(location = 2) flat out uint outTextureLayer;
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) flat out float outChunkScale;
layout(location = 5) out float outWaterDepth;

// Mock data, should be filled by using specialization
layout(constant_id = 0) const int RESOLUTION = 64;
// constant_id 1 deliberately unused - GRID_SCALE moved to a per-instance field (currentChunk.scale)
// since chunk world-size now varies per LOD ring.
layout(constant_id = 2) const float HEIGHT_SCALE = 210;
// TerrainManager::SeaLevel (normalized) - the water surface.
layout(constant_id = 3) const float SEA_LEVEL = 0.12;

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

    // texelFetch, not texture(): each vertex maps 1:1 onto one heightmap texel, and a LINEAR
    // sample at u = x/(RES-1) lands off the texel centers ((x+0.5)/RES), blending neighbor heights
    // into every vertex.
    float height = texelFetch(heightmapSampler, ivec3(xIndex, zIndex, int(currentChunk.TextureLayer)), 0).r;
    // Terrain below sea level renders as a flat water surface at SEA_LEVEL; the true seafloor depth
    // below it is passed on so terrain.frag can tint deep water darker than shallows.
    float seaLevelY = SEA_LEVEL * HEIGHT_SCALE;
    float terrainY = height * HEIGHT_SCALE;
    outWaterDepth = seaLevelY - terrainY;
    vec3 finalWorldPos = vec3(localZ + chunkOffsetX, max(terrainY, seaLevelY), localX + chunkOffsetZ);

    gl_Position = sceneGlobalsData.data.ViewProjection * vec4(finalWorldPos, 1.0);
    outWorldPos = finalWorldPos;

    // Normals are computed per pixel in terrain.frag (from the same heightmap), not here - a
    // per-vertex normal interpolated across a whole triangle loses all shading detail below
    // triangle size.
    outTextureLayer = currentChunk.TextureLayer;
    outChunkScale = currentChunk.scale;

    bool checker = ((currentChunk.WorldPos.x + currentChunk.WorldPos.y) % 2) == 0;
    debugColor = checker ? vec3(0.8, 0.2, 0.2) : vec3(0.2, 0.2, 0.8);
}
