#version 450

// Two triangles covering exactly the four real screen corners - same shape as sky.vert, and for
// the same reason: an oversized single "fullscreen triangle" can overflow some rasterizers'
// guard-band precision and clip a sliver near one corner.
const vec2 CORNERS[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

void main() {
    gl_Position = vec4(CORNERS[gl_VertexIndex], 0.0, 1.0);
}
