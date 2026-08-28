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

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec3 debugColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

// Mock data, should be filled by using specialization
layout(constant_id = 0) const float GRID_CELLS = 63.0;
layout(constant_id = 2) const float HEIGHT_SCALE = 210;

// Debug view toggle (TerrainPass::ShowChunkDebugColors, ImGui-driven) - a runtime push constant
// rather than a specialization constant specifically so it can flip without a pipeline rebuild.
layout(push_constant) uniform TerrainPushConstants {
    uint showDebugColors;
} pc;

// Cheap hash-based value noise (2D) - breaks up otherwise perfectly flat height/slope color bands
// without needing an actual texture asset, consistent with terrain height itself being procedural.
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// Classic height/slope "splat" blend - grass on gentle low-to-mid ground, rock on steep or
// mid-high ground, snow at the highest elevations - driven entirely by the same heightmap-derived
// normal/world position terrain.vert already computes, no texture sampling involved.
vec3 proceduralTerrainColor(vec3 normal, vec3 worldPos) {
    float slope = 1.0 - clamp(normal.y, 0.0, 1.0);           // 0 = flat, 1 = vertical
    float heightFrac = clamp(worldPos.y / HEIGHT_SCALE, 0.0, 1.0);

    vec3 grassColor = vec3(0.30, 0.45, 0.18);
    vec3 rockColor  = vec3(0.40, 0.36, 0.32);
    vec3 snowColor  = vec3(0.92, 0.94, 0.97);

    float grassAmount = smoothstep(0.55, 0.30, slope) * smoothstep(0.75, 0.50, heightFrac);
    float snowAmount = smoothstep(0.62, 0.82, heightFrac) * smoothstep(0.65, 0.30, slope);
    float rockAmount = clamp(1.0 - grassAmount - snowAmount, 0.0, 1.0);

    vec3 blended = grassColor * grassAmount + rockColor * rockAmount + snowColor * snowAmount;

    // Subtle world-space noise variation, not per-chunk-local UV, so the pattern stays continuous
    // across chunk boundaries instead of visibly repeating per chunk.
    float n = valueNoise(worldPos.xz * 0.03);
    return blended * (0.88 + 0.24 * n);
}

void main()
{
    // Scale UVs to grid space (0..63)
    vec2 pos = texCoord * GRID_CELLS;

    // Compute "Pixel Width" in grid units
    // fwidth: How much does 'pos' change between this pixel and the neighbor
    // This allows us to keep lines 1 pixel thick visually.
    vec2 derivative = fwidth(pos);

    // Distance to nearest line
    // Highlight where 'pos' is close to an integer.
    // Centers the line on the integer boundary.
    vec2 grid = abs(fract(pos - 0.5) - 0.5) / derivative;

    // Combine X and Z lines
    // 'line' is the distance (in pixels) to the closest line
    float line = min(grid.x, grid.y);

    // If within 1.0 pixel of the line, draw it
    // 1.0 = Center of line, 0.0 = Background
    float debugGrid = pc.showDebugColors != 0 ? 1.0 - min(line, 1.0) : 0.0;

    // Same Lambertian diffuse + ambient model prop.frag uses, for visual consistency between
    // terrain and props - SunDirection already points FROM the scene TOWARD the sun (see
    // prop.frag's own note), so no negation needed.
    vec3 normal = normalize(inNormal);
    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);
    float ndotl = max(dot(normal, sunDir), 0.0);
    float lighting = 0.35 + ndotl * 0.65;

    vec3 baseColor = pc.showDebugColors != 0
        ? debugColor
        : proceduralTerrainColor(normal, inWorldPos);

    // Color mixing
    // Background = baseColor, shaded by terrain lighting
    // Line = White (vec3(1.0)) - left unshaded so the debug grid stays a clean, unambiguous
    // reference regardless of lighting (still useful for chunk-boundary/LOD debugging).
    vec3 finalColor = mix(baseColor * lighting, vec3(1.0), debugGrid);

    // Distance fog hides the pop where the streamed terrain disk ends and SkyPass's own
    // near-black background fill begins; density tuned by eye against TotalCoverageRadius
    // (~16000 units). Applied after the grid-line mix so distant lines fade too.
    const float FOG_DENSITY = 1.0 / 9000.0;
    const vec3 FOG_COLOR = vec3(0.55, 0.60, 0.65);
    float dist = length(inWorldPos - sceneGlobalsData.data.CameraPosition);
    float fogFactor = clamp(1.0 - exp(-dist * FOG_DENSITY), 0.0, 1.0);
    finalColor = mix(finalColor, FOG_COLOR, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
