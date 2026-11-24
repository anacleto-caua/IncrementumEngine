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

layout(set = 1, binding = 1) uniform sampler2DArray heightmapSampler;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec3 debugColor;
layout(location = 2) flat in uint inTextureLayer;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in float inChunkScale;
layout(location = 5) in float inWaterDepth;   // > 0 = this pixel is water, this deep

layout(location = 0) out vec4 outColor;

// Mock data, should be filled by using specialization
layout(constant_id = 0) const float GRID_CELLS = 63.0;
layout(constant_id = 2) const float HEIGHT_SCALE = 210;
layout(constant_id = 3) const float SEA_LEVEL = 0.12;
// Distance at which terrain is fully fogged - TerrainManager::TotalCoverageRadius, so the edge of
// the streamed terrain disk dissolves into the sky instead of ending on a visible line.
layout(constant_id = 10) const float FOG_END = 16000.0;

// Heightmap array layout (TerrainManager::MaxCachedChunks / OuterRingMaxCachedChunks): ring 0's
// layers first, then one equally-sized block per outer ring - same as GetPropsForLayer().
layout(constant_id = 11) const uint RING0_LAYERS = 634;
layout(constant_id = 12) const uint OUTER_RING_LAYERS = 300;
// TerrainPass::RingColors, sRGB 0xRRGGBB, ring 0 first.
layout(constant_id = 20) const uint RING_COLOR_0 = 0x482878;
layout(constant_id = 21) const uint RING_COLOR_1 = 0x31688E;
layout(constant_id = 22) const uint RING_COLOR_2 = 0x1F9E89;
layout(constant_id = 23) const uint RING_COLOR_3 = 0x6DCD59;
layout(constant_id = 24) const uint RING_COLOR_4 = 0xFDE725;

// Debug view toggles (TerrainPass's Show*/FogEnabled, ImGui-driven) - a runtime push constant
// rather than specialization constants specifically so they flip without a pipeline rebuild.
// Bits mirror TerrainPass::PushFlags.
const uint FLAG_CHUNK_DEBUG_COLORS = 1u << 0;
const uint FLAG_RING_COLORS        = 1u << 1;
const uint FLAG_CHUNK_OUTLINES     = 1u << 2;
const uint FLAG_WIREFRAME          = 1u << 3;
const uint FLAG_NO_FOG             = 1u << 4;
const uint FLAG_CAMERA_MARKER      = 1u << 5;

layout(push_constant) uniform TerrainPushConstants {
    uint flags;
} pc;

bool HasFlag(uint flag) { return (pc.flags & flag) != 0u; }

#include "include/atmosphere.glsl"

// --- Noise ---------------------------------------------------------------------------------------
// Integer-hash gradient noise with analytic derivatives. Integer hashing (pcg2d) instead of the
// classic fract(sin(...)) hash, which bands/breaks down at large world coordinates on some GPUs.

uvec2 Pcg2d(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return v;
}

vec2 Gradient(ivec2 cell) {
    float angle = float(Pcg2d(uvec2(cell)).x) * (2.0 * PI / 4294967296.0);
    return vec2(cos(angle), sin(angle));
}

// x = noise value (~[-0.7, 0.7]), yz = d(noise)/d(p)
vec3 GradientNoise(vec2 p) {
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p);

    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);

    vec2 ga = Gradient(i);
    vec2 gb = Gradient(i + ivec2(1, 0));
    vec2 gc = Gradient(i + ivec2(0, 1));
    vec2 gd = Gradient(i + ivec2(1, 1));

    float va = dot(ga, f);
    float vb = dot(gb, f - vec2(1.0, 0.0));
    float vc = dot(gc, f - vec2(0.0, 1.0));
    float vd = dot(gd, f - vec2(1.0, 1.0));

    float value = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd);
    vec2 derivative = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd)
                    + du * (u.yx * (va - vb - vc + vd) + vec2(vb, vc) - va);
    return vec3(value, derivative);
}

// --- Surface -------------------------------------------------------------------------------------

// Heightmap sample at chunk-local uv, bilinear, half-texel corrected: uv 0..1 spans texel CENTERS
// 0..RES-1 (the same 1:1 vertex->texel mapping terrain.vert's texelFetch uses), not texel edges.
float SampleHeight(vec2 uv) {
    vec2 texel_uv = (clamp(uv, 0.0, 1.0) * GRID_CELLS + 0.5) / (GRID_CELLS + 1.0);
    return texture(heightmapSampler, vec3(texel_uv, float(inTextureLayer))).r * HEIGHT_SCALE;
}

// Per-pixel normal from central differences on the heightmap. Tangents follow terrain.vert's
// u/v -> world mapping exactly (u -> world.z, v -> world.x) so the normal can't end up
// axis-swapped relative to the rendered surface. At a chunk edge the difference goes one-sided
// (clamped to this chunk's own texels) instead of reading a neighbor it has no access to.
vec3 TerrainNormal(vec2 uv) {
    float e = 1.0 / GRID_CELLS;
    float u0 = max(uv.x - e, 0.0), u1 = min(uv.x + e, 1.0);
    float v0 = max(uv.y - e, 0.0), v1 = min(uv.y + e, 1.0);

    float dhU = SampleHeight(vec2(u1, uv.y)) - SampleHeight(vec2(u0, uv.y));
    float dhV = SampleHeight(vec2(uv.x, v1)) - SampleHeight(vec2(uv.x, v0));

    vec3 tangentU = vec3(0.0, dhU, (u1 - u0) * inChunkScale);
    vec3 tangentV = vec3((v1 - v0) * inChunkScale, dhV, 0.0);
    return normalize(cross(tangentU, tangentV));
}

// Adds surface relief below triangle size (the mesh is ~1.6 world units per vertex at best) via
// world-space gradient-noise bump. Each octave fades out once its wavelength drops toward a
// pixel's own world footprint, so distant terrain stays stable instead of shimmering.
vec3 DetailNormal(vec3 normal, vec3 worldPos, float roughness) {
    float footprint = max(length(fwidth(worldPos.xz)), 1e-4);

    vec2 slopeGradient = vec2(0.0);
    float frequency = 0.11;
    float amplitude = 1.3;   // world units of bump height at the first octave
    for (int i = 0; i < 4; i++) {
        float fade = 1.0 - smoothstep(0.12, 0.45, footprint * frequency);
        if (fade <= 0.0) { break; }
        slopeGradient += GradientNoise(worldPos.xz * frequency + float(i) * 17.3).yz * frequency * amplitude * fade;
        frequency *= 2.13;
        amplitude *= 0.45;
    }
    slopeGradient *= roughness;

    return normalize(normal + vec3(-slopeGradient.x, 0.0, -slopeGradient.y));
}

// Height/slope-driven material blend: sand at the lowest ground, lush-to-dry grass varied by large
// world-space patches, dirt on moderate slopes, banded rock strata on steep or high ground, and
// snow that only settles where the surface faces up. All palette values are authored in sRGB and
// converted to linear here - lighting math on sRGB values is what washed the old colors out.
// Returns rgb = albedo, a = rockiness (drives detail-bump strength).
vec4 TerrainAlbedo(vec3 macroNormal, vec3 worldPos) {
    float slope = 1.0 - clamp(macroNormal.y, 0.0, 1.0);   // 0 = flat, 1 = vertical
    float heightFrac = clamp(worldPos.y / HEIGHT_SCALE, 0.0, 1.0);

    float macroNoise = GradientNoise(worldPos.xz * 0.0021).x * 0.7 + 0.5;
    float midNoise = GradientNoise(worldPos.xz * 0.027).x + 0.5 * GradientNoise(worldPos.xz * 0.061).x;
    float jitteredHeight = heightFrac + midNoise * 0.03;

    vec3 sand      = SrgbToLinear(vec3(0.74, 0.68, 0.50));
    vec3 grassLush = SrgbToLinear(vec3(0.23, 0.37, 0.11));
    vec3 grassDry  = SrgbToLinear(vec3(0.47, 0.46, 0.22));
    vec3 dirt      = SrgbToLinear(vec3(0.39, 0.31, 0.22));
    vec3 rockDark  = SrgbToLinear(vec3(0.26, 0.24, 0.22));
    vec3 rockLight = SrgbToLinear(vec3(0.52, 0.49, 0.45));
    vec3 snow      = SrgbToLinear(vec3(0.93, 0.95, 0.98));

    // Grass dries out with altitude and in large dry patches.
    vec3 grass = mix(grassLush, grassDry, smoothstep(0.35, 0.85, macroNoise + jitteredHeight * 0.8));

    // Sedimentary-looking strata: bands follow world height, wobbled by noise so they aren't
    // perfectly flat lines.
    float strata = sin(worldPos.y * 0.3 + midNoise * 3.0) * 0.5 + 0.5;
    vec3 rock = mix(rockDark, rockLight, clamp(strata * 0.65 + macroNoise * 0.35, 0.0, 1.0));

    float rockWeight = smoothstep(0.26, 0.42, slope + midNoise * 0.04);
    rockWeight = max(rockWeight, smoothstep(0.58, 0.72, jitteredHeight));
    float dirtWeight = smoothstep(0.14, 0.26, slope + midNoise * 0.03) * (1.0 - rockWeight);
    // Beaches: a narrow band just above the waterline, not a fixed absolute height.
    float heightAboveSea = worldPos.y - SEA_LEVEL * HEIGHT_SCALE;
    float sandWeight = (1.0 - smoothstep(3.0, 12.0, heightAboveSea + midNoise * 5.0)) * (1.0 - rockWeight);
    float snowWeight = smoothstep(0.62, 0.74, jitteredHeight) * smoothstep(0.62, 0.85, macroNormal.y);

    vec3 albedo = mix(grass, dirt, dirtWeight);
    albedo = mix(albedo, rock, rockWeight);
    albedo = mix(albedo, sand, sandWeight);
    albedo = mix(albedo, snow, snowWeight);

    // Fine brightness variation so large uniform areas don't read as flat paint.
    albedo *= 0.9 + 0.2 * (GradientNoise(worldPos.xz * 0.19).x + 0.5);

    // Grass/sand get only a faint bump - open ground should read smooth, rock carries the relief.
    float rockiness = mix(0.12, 1.0, rockWeight) * (1.0 - 0.7 * snowWeight);
    return vec4(albedo, rockiness);
}

// Water surface: depth-tinted body color, Fresnel-weighted reflection of the actual sky (same
// atmosphere model, reflected view ray), a sun glint and a thin foam line at the shore. Waves are a
// static normal-only gradient-noise bump (no time input yet), footprint-faded like DetailNormal.
// `footprint` comes from main(): fwidth() is undefined inside the non-uniform (per-pixel water vs
// land) branch this is called from.
vec3 WaterColor(vec3 worldPos, float depth, vec3 sunDir, float footprint) {
    vec3 cameraPos = sceneGlobalsData.data.CameraPosition;
    vec3 viewDir = normalize(worldPos - cameraPos);

    vec2 waveGradient = vec2(0.0);
    float frequency = 0.045;
    float amplitude = 0.5;
    for (int i = 0; i < 3; i++) {
        float fade = 1.0 - smoothstep(0.12, 0.45, footprint * frequency);
        waveGradient += GradientNoise(worldPos.xz * frequency + float(i) * 7.1).yz * frequency * amplitude * fade;
        frequency *= 2.7;
        amplitude *= 0.4;
    }
    vec3 normal = normalize(vec3(-waveGradient.x, 1.0, -waveGradient.y));

    float cosTheta = max(dot(-viewDir, normal), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);

    vec3 reflectDir = reflect(viewDir, normal);
    reflectDir = normalize(vec3(reflectDir.x, max(reflectDir.y, 0.02), reflectDir.z));
    vec3 skyReflection = Atmosphere(reflectDir, sunDir, 6, 3);

    vec3 shallow = SrgbToLinear(vec3(0.10, 0.42, 0.42));
    vec3 deep = SrgbToLinear(vec3(0.02, 0.09, 0.17));
    vec3 body = mix(shallow, deep, 1.0 - exp(-depth * 0.05));
    vec3 up = vec3(0.0, 1.0, 0.0);
    body *= SunLight(up, sunDir) * 0.6 + AmbientLight(up, sunDir);

    vec3 color = mix(body, skyReflection, fresnel);

    float glint = pow(max(dot(reflectDir, sunDir), 0.0), 600.0);
    color += glint * SUN_LIGHT_INTENSITY * 10.0 * SunTransmittance(sunDir);

    float foam = 1.0 - smoothstep(0.3, 2.0, depth + GradientNoise(worldPos.xz * 0.2).x * 0.8);
    vec3 foamColor = SrgbToLinear(vec3(0.9)) * (SunLight(up, sunDir) + AmbientLight(up, sunDir));
    return mix(color, foamColor, foam * 0.7);
}

// --- LOD ring debug view -------------------------------------------------------------------------

uint RingIndex(uint layer) {
    return layer < RING0_LAYERS ? 0u : 1u + (layer - RING0_LAYERS) / OUTER_RING_LAYERS;
}

vec3 RingColor(uint ring) {
    uint rgb = ring == 0u ? RING_COLOR_0
             : ring == 1u ? RING_COLOR_1
             : ring == 2u ? RING_COLOR_2
             : ring == 3u ? RING_COLOR_3
             : RING_COLOR_4;
    return SrgbToLinear(vec3((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu) / 255.0);
}

// Antialiased coverage of a line `halfWidth` pixels to each side of every point where `value`
// crosses an integer. Distance is measured along value's screen gradient (not fwidth, which
// overestimates it on diagonals), so lines keep one width at any orientation and distance.
float LineCoverage(float value, float halfWidth) {
    float gradient = max(length(vec2(dFdx(value), dFdy(value))), 1e-6);
    float pixels = abs(fract(value - 0.5) - 0.5) / gradient;
    return 1.0 - smoothstep(halfWidth - 0.5, halfWidth + 0.5, pixels);
}

// LineCoverage for one family of parallel mesh edges, faded out where consecutive lines are closer
// than ~3 pixels on screen - there they'd only merge into a flat tone or moiré instead of showing
// individual edges. Per family, so at grazing angles the edges running toward the camera (still
// well apart on screen) keep showing after the foreshortened cross edges have faded.
float WireCoverage(float value) {
    float spacing = 1.0 / max(length(vec2(dFdx(value), dFdy(value))), 1e-6);
    return LineCoverage(value, 0.35) * smoothstep(2.0, 4.0, spacing);
}

void main()
{
    bool chunkColors = HasFlag(FLAG_CHUNK_DEBUG_COLORS);
    bool ringColors = HasFlag(FLAG_RING_COLORS);
    bool flatAlbedo = chunkColors || ringColors;
    float footprint = max(length(fwidth(inWorldPos.xz)), 1e-4);
    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);

    vec3 macroNormal = TerrainNormal(texCoord);

    vec3 albedo;
    vec3 normal;
    if (ringColors) {
        albedo = RingColor(RingIndex(inTextureLayer));
        // The water surface is flat - lighting it with the seafloor's normal would print the
        // seafloor's relief onto the sea.
        normal = inWaterDepth > 0.0 ? vec3(0.0, 1.0, 0.0) : macroNormal;
    } else if (chunkColors) {
        albedo = debugColor;
        normal = macroNormal;
    } else {
        vec4 material = TerrainAlbedo(macroNormal, inWorldPos);
        albedo = material.rgb;
        normal = DetailNormal(macroNormal, inWorldPos, material.a);
    }

    vec3 color = albedo * (SunLight(normal, sunDir) + AmbientLight(normal, sunDir));
    if (!flatAlbedo && inWaterDepth > 0.0) {
        color = WaterColor(inWorldPos, inWaterDepth, sunDir, footprint);
    }

    // Triangle edges of the 64x64 vertex mesh: grid lines along u and v, plus each cell's diagonal
    // (TerrainPass's index buffer splits cells top-right -> bottom-left, i.e. along u + v = const).
    // Thin, translucent, and faded where too dense to resolve (WireCoverage), so the surface keeps
    // its tint. Lines keep the surface's hue - darker on light albedo, lighter on dark albedo (the
    // two darkest ring colors), so they read on every ring.
    if (HasFlag(FLAG_WIREFRAME)) {
        vec2 cell = texCoord * GRID_CELLS;
        float wire = max(max(WireCoverage(cell.x), WireCoverage(cell.y)), WireCoverage(cell.x + cell.y));
        bool darkAlbedo = dot(albedo, vec3(0.2126, 0.7152, 0.0722)) < 0.15;
        vec3 lineColor = darkAlbedo ? color * 3.0 : color * 0.2;
        color = mix(color, lineColor, wire * 0.7);
    }
    // Chunk borders (texCoord 0 or 1). Each chunk draws its own half, so the shared border line is
    // twice halfWidth wide.
    if (HasFlag(FLAG_CHUNK_OUTLINES)) {
        float border = max(LineCoverage(texCoord.x, 0.5), LineCoverage(texCoord.y, 0.5));
        color = mix(color, vec3(0.0), border * 0.85);
    }

    if (!HasFlag(FLAG_NO_FOG)) {
        color = ApplyAerialPerspective(color, inWorldPos, sceneGlobalsData.data.CameraPosition, sunDir, FOG_END);
    }

    // Camera position marker for the top-down view: a white dot with a dark rim, sized in pixels
    // (world units per pixel from the screen gradient of world X). Bright enough (radiance 10) to
    // stay white through PostPass's tonemap.
    if (HasFlag(FLAG_CAMERA_MARKER)) {
        float worldPerPixel = max(length(vec2(dFdx(inWorldPos.x), dFdy(inWorldPos.x))), 1e-4);
        float pixels = distance(inWorldPos.xz, sceneGlobalsData.data.CameraPosition.xz) / worldPerPixel;
        float rim = 1.0 - smoothstep(8.0, 9.0, pixels);
        float fill = 1.0 - smoothstep(5.0, 6.0, pixels);
        color = mix(color, vec3(0.0), rim);
        color = mix(color, vec3(10.0), fill);
    }

    // Chunk grid overlay (chunk debug view only). Scale UVs to grid space (0..63); fwidth keeps
    // lines one pixel thick regardless of distance. Unshaded white (1.0 lands near-white after
    // PostPass's tonemap) so it stays a clean reference for chunk-boundary/LOD debugging.
    if (chunkColors) {
        vec2 pos = texCoord * GRID_CELLS;
        vec2 grid = abs(fract(pos - 0.5) - 0.5) / fwidth(pos);
        float line = min(grid.x, grid.y);
        color = mix(color, vec3(1.0), 1.0 - min(line, 1.0));
    }

    outColor = vec4(color, 1.0);
}
