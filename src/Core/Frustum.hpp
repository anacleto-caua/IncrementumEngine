#pragma once

#include "Core/Math.hpp"

struct AABB {
    vec3 Min;
    vec3 Max;
};

// 6 inward-facing planes (ax + by + cz + d >= 0 means "on the inside" of that plane),
// Gribb-Hartmann extracted from a combined view-projection matrix.
struct Frustum {
    enum Side { Left, Right, Bottom, Top, Near, Far, _COUNT_ };
    vec4 Planes[Side::_COUNT_];
};

inline Frustum ExtractFrustum(const mat4& view_projection) {
    Frustum f;
    const mat4& m = view_projection;

    // Rows of m (glm is column-major: m[col][row]). Vulkan clip space with
    // GLM_FORCE_DEPTH_ZERO_TO_ONE: inside is -w<=x<=w, -w<=y<=w, 0<=z<=w.
    vec4 row0 = { m[0][0], m[1][0], m[2][0], m[3][0] };
    vec4 row1 = { m[0][1], m[1][1], m[2][1], m[3][1] };
    vec4 row2 = { m[0][2], m[1][2], m[2][2], m[3][2] };
    vec4 row3 = { m[0][3], m[1][3], m[2][3], m[3][3] };

    f.Planes[Frustum::Left]   = row3 + row0;
    f.Planes[Frustum::Right]  = row3 - row0;
    f.Planes[Frustum::Bottom] = row3 + row1;
    f.Planes[Frustum::Top]    = row3 - row1;
    f.Planes[Frustum::Near]   = row2;
    f.Planes[Frustum::Far]    = row3 - row2;

    for (auto& p : f.Planes) {
        f32 len = math::length(vec3(p.x, p.y, p.z));
        p /= len;
    }
    return f;
}

// Conservative: true if the box is at least partially inside (never a false negative; may be a
// false positive right at the frustum's edges/corners - the standard tradeoff for this test).
inline bool Intersects(const Frustum& frustum, const AABB& box) {
    for (const vec4& p : frustum.Planes) {
        vec3 p_vertex = {
            p.x >= 0.0f ? box.Max.x : box.Min.x,
            p.y >= 0.0f ? box.Max.y : box.Min.y,
            p.z >= 0.0f ? box.Max.z : box.Min.z,
        };
        if (math::dot(vec3(p.x, p.y, p.z), p_vertex) + p.w < 0.0f) { return false; }
    }
    return true;
}
