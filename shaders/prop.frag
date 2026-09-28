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

// Still bound (PropPass keeps the placeholder texture wired up for when real UV-authored prop art
// lands), but deliberately not sampled: the placeholder is a magenta/black "missing texture"
// checker, and neither placeholder .obj carries UVs to map it properly anyway.
layout(set = 1, binding = 1) uniform sampler2DArray propTexture;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

// Same value terrain.frag uses - props must fade at exactly the rate of the ground they sit on.
layout(constant_id = 10) const float FOG_END = 16000.0;

#include "include/atmosphere.glsl"

void main() {
    vec3 normal = normalize(inNormal);
    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);

    // Base color is authored in sRGB (PropPass::Init()) - convert before lighting, same as terrain.
    vec3 albedo = SrgbToLinear(inColor);

    vec3 color = albedo * (SunLight(normal, sunDir) + AmbientLight(normal, sunDir));
    color = ApplyAerialPerspective(color, inWorldPos, sceneGlobalsData.data.CameraPosition, sunDir, FOG_END);

    outColor = vec4(color, 1.0);
}
