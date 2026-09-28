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

layout(location = 0) in vec3 inRayDir;
layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const int PRIMARY_STEPS = 16;
layout(constant_id = 1) const int SECONDARY_STEPS = 8;

#include "include/atmosphere.glsl"

void main() {
    vec3 sunDir = normalize(sceneGlobalsData.data.SunDirection);
    vec3 color = Atmosphere(normalize(inRayDir), sunDir, PRIMARY_STEPS, SECONDARY_STEPS);

    // Unbounded linear radiance - PostPass tonemaps the whole scene once.
    outColor = vec4(color, 1.0);
}
