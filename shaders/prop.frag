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

layout(set = 1, binding = 1) uniform sampler2DArray propTexture;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(inNormal);
    // SunDirection points FROM the scene TOWARD the sun (elevation=90 -> (0,1,0), straight up -
    // same convention sky.frag's slider derivation uses), so it's already the "L" vector a
    // Lambertian term wants, no negation needed.
    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);

    float ndotl = max(dot(normal, sunDir), 0.0);

    float ambient = 0.35;
    float diffuse = ndotl * 0.65;

    // World-space planar UV (not real per-vertex UVs - see PropPass.hpp's TextureImage comment):
    // neither placeholder .obj carries texture coordinates yet, so this stands in until real,
    // UV-authored prop art replaces them.
    vec2 uv = inWorldPos.xz * 0.2;
    vec3 texColor = texture(propTexture, vec3(uv, 0.0)).rgb;

    vec3 albedo = inColor * texColor;
    vec3 litColor = albedo * (ambient + diffuse);

    // Same distance fog terrain.frag applies, same constants - props fading at a different rate
    // than the ground they sit on would look wrong. See terrain.frag's own note for why/how tuned.
    const float FOG_DENSITY = 1.0 / 9000.0;
    const vec3 FOG_COLOR = vec3(0.55, 0.60, 0.65);
    float dist = length(inWorldPos - sceneGlobalsData.data.CameraPosition);
    float fogFactor = clamp(1.0 - exp(-dist * FOG_DENSITY), 0.0, 1.0);
    vec3 finalColor = mix(litColor, FOG_COLOR, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
