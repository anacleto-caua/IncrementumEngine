#pragma once

#include "Core/Math.hpp"

struct Camera {
    // Spatial
    vec3 Position = vec3::ZERO;
    vec3 Front = vec3::FORWARD;
    vec3 Up = vec3::UP;

    // Lens State
    f32 Fov = 90.0f;
    f32 AspectRatio = 16.0f / 9.0f;
    f32 NearPlane = 0.1f;
    f32 FarPlane = 1000.0f;

    // Output
    mat4 View = mat4(1.0f);
    mat4 Projection = mat4(1.0f);
};

// Updates the View and Projection based on current state.
inline void UpdateMatrices(Camera &camera) {
    camera.View = math::lookAt(
        camera.Position,
        camera.Position + camera.Front,
        camera.Up
    );

    camera.Projection = math::perspective(
        math::radians(camera.Fov),
        camera.AspectRatio,
        camera.NearPlane,
        camera.FarPlane
    );

    // Force the Y-flip for Vulkan
    camera.Projection[1][1] *= -1.0f;
}

inline Camera CreateCamera(f32 aspect, f32 fov) {
    Camera cam;
    cam.AspectRatio = aspect;
    cam.Fov = fov;

    UpdateMatrices(cam);
    return cam;
}

// Updates the aspect ratio and recalculates the matrices
inline void Resize(Camera &camera, f32 new_aspect) {
    camera.AspectRatio = new_aspect;
    UpdateMatrices(camera);
}

// Snaps the camera to look at a specific point in space
inline void LookAt(Camera &camera, vec3 target) {
    camera.Front = math::normalize(target - camera.Position);
    camera.Up = math::normalize(math::cross(vec3::RIGHT, camera.Front));

    UpdateMatrices(camera);
}
