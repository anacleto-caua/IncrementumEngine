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

Camera CreateCamera();
Camera CreateCamera(f32 aspect, f32 fov);

// Updates the View and Projection based on current state.
void UpdateMatrices(Camera &camera);

// Updates the aspect ratio and recalculates the matrices
void Resize(Camera &camera, f32 new_aspect);

// Snaps the camera to look at a specific point in space
void LookAt(Camera &camera, vec3 target);
