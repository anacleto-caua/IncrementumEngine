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

layout(location = 0) out vec3 outRayDir;

// Two triangles covering exactly the four real screen corners
const vec2 CORNERS[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

void main() {
    vec2 ndc = CORNERS[gl_VertexIndex];
    gl_Position = vec4(ndc, 1.0, 1.0); // far plane - always behind real geometry

    vec4 world = sceneGlobalsData.data.InverseViewProjection * vec4(ndc, 1.0, 1.0);
    outRayDir = normalize(world.xyz / world.w - sceneGlobalsData.data.CameraPosition);
}
