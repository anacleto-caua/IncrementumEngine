#version 450

// Resolved (already MSAA-averaged) HDR scene color - linear, unbounded radiance.
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform PostPushConstants {
    float exposure;
} pc;

layout(location = 0) out vec4 outColor;

// Stephen Hill's fitted ACES (RRT + ODT) approximation. Compared with the plain Reinhard every
// shader used before: similar mid-tones, but a real toe (richer shadows), a gentle shoulder that
// keeps bright sky and snow from flattening to grey-white, and highlights desaturating toward white
// instead of clipping hue. Matrices are column-major (GLSL mat3 constructor order) transposes of
// the published row-major ones.
const mat3 ACES_INPUT = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);
const mat3 ACES_OUTPUT = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
);

vec3 RrtAndOdtFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 AcesFitted(vec3 color) {
    color = ACES_INPUT * color;
    color = RrtAndOdtFit(color);
    color = ACES_OUTPUT * color;
    return clamp(color, 0.0, 1.0);
}

void main() {
    // Scene target and swapchain are always the same size, so this is an exact 1:1 read.
    vec3 hdr = texelFetch(sceneColor, ivec2(gl_FragCoord.xy), 0).rgb;

    // Output stays LINEAR - the _SRGB swapchain format applies the sRGB encode on write.
    outColor = vec4(AcesFitted(hdr * pc.exposure), 1.0);
}
