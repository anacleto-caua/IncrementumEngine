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

// Matches PropPass::GpuInstance exactly - two vec4s, no manually-computed std430 padding needed.
struct PropInstance {
    vec4 PositionAndRotation;  // xyz = world position, w = Y rotation (radians)
    vec4 ScaleAndColor;        // x = scale, yzw = flat base color (RGB)
};

layout(std430, set = 1, binding = 0) readonly buffer InstanceBuffer {
    PropInstance instances[];
} instanceData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec3 outWorldPos;

void main() {
    PropInstance instance = instanceData.instances[gl_InstanceIndex];

    float c = cos(instance.PositionAndRotation.w);
    float s = sin(instance.PositionAndRotation.w);

    vec3 scaledPos = inPosition * instance.ScaleAndColor.x;
    // Y-axis-only rotation - props don't need full 3-axis orientation.
    vec3 rotatedPos = vec3(
        scaledPos.x * c + scaledPos.z * s,
        scaledPos.y,
        -scaledPos.x * s + scaledPos.z * c
    );
    vec3 worldPos = rotatedPos + instance.PositionAndRotation.xyz;

    vec3 rotatedNormal = vec3(
        inNormal.x * c + inNormal.z * s,
        inNormal.y,
        -inNormal.x * s + inNormal.z * c
    );

    outNormal = rotatedNormal;
    outColor = instance.ScaleAndColor.yzw;
    outWorldPos = worldPos;

    gl_Position = sceneGlobalsData.data.ViewProjection * vec4(worldPos, 1.0);
}
