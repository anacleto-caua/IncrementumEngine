#include "Camera.hpp"

#include <imgui.h>

#include "Core/Math.hpp"
#include "Engine/Engine.hpp"

Camera CreateCamera() {
    return
        CreateCamera(
            static_cast<f32>(Engine::Config.Width) / static_cast<f32>(Engine::Config.Height),
            static_cast<f32>(Engine::Config.FOV)
        );
}

Camera CreateCamera(f32 aspect, f32 fov) {
    Camera cam;
    cam.AspectRatio = aspect;
    cam.Fov = fov;

    UpdateMatrices(cam);
    return cam;
}

void UpdateMatrices(Camera &camera) {
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

void Resize(Camera &camera, f32 new_aspect) {
    camera.AspectRatio = new_aspect;
    UpdateMatrices(camera);
}

void LookAt(Camera &camera, vec3 target) {
    camera.Front = math::normalize(target - camera.Position);

    // You may also want to recalculate the 'Up' vector here using cross products
    // against a global 'right' vector to ensure the camera doesn't bank weirdly.
    // It would be ok for simple targeting but it's required for space games or so.
    camera.Up = math::normalize(math::cross(vec3::RIGHT, camera.Front));

    UpdateMatrices(camera);
}
