#include "Camera.hpp"

#include <imgui.h>

#include "Core/Math.hpp"
#include "Engine/Engine.hpp"

Camera3D CreateCamera3D() {
    return
        CreateCamera3D(
            static_cast<f32>(Engine::Config.Width) / static_cast<f32>(Engine::Config.Height),
            static_cast<f32>(Engine::Config.FOV)
        );
}

Camera3D CreateCamera3D(f32 Aspect, f32 Fov) {
    Camera3D cam;
    cam.FocalLength = 1.0f / tan(math::radians(Fov) / 2.0f);
    cam.Position = vec3::ZERO;
    cam.LookDir = vec3::FORWARD;
    cam.Model = mat4(1.0f);
    cam.Projection = math::perspective(math::radians(Fov), Aspect, 0.1f, 1000.0f);
    cam.Projection[1][1] *= -1.0f; // Vulkan Y-flip
    cam.ModelViewProjection = 0;
    RefreshMVP(cam);
    return cam;
}

void RefreshMVP(Camera3D &Camera) {
    Camera.ModelViewProjection = Camera.Projection * Camera.View * Camera.Model;
}

void Resize(Camera3D &Camera, f32 NewAspect) {
    Camera.Projection[0][0] = Camera.FocalLength / NewAspect;
    RefreshMVP(Camera);
}

void Move(Camera3D &Camera) {
    Camera.View = lookAt(Camera.Position, Camera.Position+Camera.LookDir, vec3::UP);
    RefreshMVP(Camera);
}


void LookAt(Camera3D &Camera, vec3 target) {
    Camera.LookDir = math::normalize(target - Camera.Position);
    RefreshMVP(Camera);
}
