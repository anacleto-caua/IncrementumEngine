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

// Every prop texture, one layer each (PropPass::CreateTextures()). Layer 0 is plain white, used
// by untextured submeshes, so this always samples and never branches. sRGB format, so the
// sample is already linear.
layout(set = 1, binding = 1) uniform sampler2DArray propTexture;

// Matches PropPass::PushConstants - one per submesh (material) draw.
layout(push_constant) uniform PropPushConstants {
    vec3 tint;           // linear albedo multiplier (.mtl Kd, or the model's fallback color)
    uint layerAndFlags;  // texture layer in the low bits | FOLIAGE_FLAG
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;  // per-instance linear tint variation
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec4 outColor;

// Same value terrain.frag uses - props must fade at exactly the rate of the ground they sit on.
layout(constant_id = 10) const float FOG_END = 16000.0;

#include "include/atmosphere.glsl"

const float ALPHA_CUTOFF = 0.5;
const uint FOLIAGE_FLAG = 1u << 31;
// Fraction of sunlight a leaf card passes through to its unlit side.
const float FOLIAGE_TRANSMISSION = 0.45;

void main() {
    bool foliage = (pc.layerAndFlags & FOLIAGE_FLAG) != 0u;
    uint textureLayer = pc.layerAndFlags & ~FOLIAGE_FLAG;
    vec4 texel = texture(propTexture, vec3(inUV, float(textureLayer)));

    // Alpha-to-coverage with a sharpened alpha: rescaling around the cutoff by the alpha's own
    // screen-space derivative turns the soft texture alpha into a ~1 pixel wide ramp, so edges
    // are crisp up close and antialiased by the MSAA samples instead of a blurry or aliased
    // alpha test. Fully opaque texels have fwidth == 0 and land at exactly 1 (full coverage).
    float alpha = (texel.a - ALPHA_CUTOFF) / max(fwidth(texel.a), 0.0001) + 0.5;
    alpha = clamp(alpha, 0.0, 1.0);
    if (alpha <= 0.0) { discard; }

    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);
    vec3 viewDir = normalize(sceneGlobalsData.data.CameraPosition - inWorldPos);

    // Props are drawn two-sided (no backface culling) so single-sided leaf cards show from both
    // sides; face the normal toward the viewer so the back of a card is lit like its front.
    vec3 normal = normalize(inNormal);
    if (dot(normal, viewDir) < 0.0) { normal = -normal; }

    vec3 albedo = texel.rgb * pc.tint * inColor;

    vec3 light;
    if (foliage) {
        // Thin translucent cards: the side facing away from the sun still gets transmitted
        // sunlight, and ambient is biased toward the sky - a card's geometric normal says little
        // about how much sky a canopy sees, and a pure downward normal would only get the dark
        // ground bounce (which is what made branch undersides read as black).
        light = SunLight(normal, sunDir) + FOLIAGE_TRANSMISSION * SunLight(-normal, sunDir)
              + AmbientLight(normalize(normal + vec3(0.0, 1.5, 0.0)), sunDir);
    } else {
        light = SunLight(normal, sunDir) + AmbientLight(normal, sunDir);
    }
    vec3 color = albedo * light;
    color = ApplyAerialPerspective(color, inWorldPos, sceneGlobalsData.data.CameraPosition, sunDir, FOG_END);

    outColor = vec4(color, alpha);
}
