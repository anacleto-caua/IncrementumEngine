// Very simple GLM wrapper
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Float vectors thing wrappers
struct vec3 : public glm::vec3 {
    constexpr vec3() : glm::vec3() {}
    constexpr vec3(float scalar) : glm::vec3(scalar) {}
    constexpr vec3(float x, float y, float z) : glm::vec3(x, y, z) {}

    // Implicit conversion from glm::vec3
    constexpr vec3(const glm::vec3& v) : glm::vec3(v) {}

    // 3. Unity-style Constants
    static constexpr glm::vec3 ZERO    = { 0.0f,  0.0f,  0.0f};
    static constexpr glm::vec3 ONE     = { 1.0f,  1.0f,  1.0f};

    static constexpr glm::vec3 UP      = { 0.0f,  1.0f,  0.0f};
    static constexpr glm::vec3 RIGHT   = { 1.0f,  0.0f,  0.0f};
    static constexpr glm::vec3 FORWARD = { 0.0f,  0.0f, 1.0f};
};

// ==========================================
// VEC2 THIN WRAPPER
// ==========================================
struct vec2 : public glm::vec2 {
    constexpr vec2() : glm::vec2() {}
    constexpr vec2(float scalar) : glm::vec2(scalar) {}
    constexpr vec2(float x, float y) : glm::vec2(x, y) {}
    constexpr vec2(const glm::vec2& v) : glm::vec2(v) {}

    static constexpr glm::vec2 ZERO  = { 0.0f,  0.0f};
    static constexpr glm::vec2 ONE   = { 1.0f,  1.0f};

    static constexpr glm::vec2 UP    = { 0.0f,  1.0f};
    static constexpr glm::vec2 RIGHT = { 1.0f,  0.0f};
};

using vec4 = glm::vec4;

// Unsigned 32-bit vectors
using uvec2 = glm::uvec2;
using uvec3 = glm::uvec3;
using uvec4 = glm::uvec4;

// Signed 32-bit integer vectors
using ivec2 = glm::ivec2;
using ivec3 = glm::ivec3;

// Matrix Types
using mat3 = glm::mat3;
using mat4 = glm::mat4;

// Quaternion
using quat = glm::quat;



namespace math {
    // Basic Math & Vector Operations
    using glm::cross;
    using glm::dot;
    using glm::normalize;
    using glm::length;
    using glm::distance;

    // Matrix Transformations
    using glm::translate;
    using glm::rotate;
    using glm::scale;
    using glm::perspective;
    using glm::ortho;
    using glm::lookAt;

    // Utilities
    using glm::radians;
    using glm::degrees;
    using glm::value_ptr;

    // Quaternion Operations
    using glm::slerp;
    using glm::conjugate;
    using glm::inverse;
    using glm::dot;
    using glm::normalize;

    // Conversions (Quat <-> Matrix)
    using glm::mat4_cast;
    using glm::quat_cast;
}
