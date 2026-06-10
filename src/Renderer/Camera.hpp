#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

struct Camera3D {
    f32 FocalLength;
    glm::vec3 Position;
    glm::vec3 LookDir;
    glm::mat4 Model;
    glm::mat4 View;
    glm::mat4 Projection;
    glm::mat4 ModelViewProjection;
};

Camera3D CreateCamera3D(f32 Aspect, f32 Fov);
void RefreshMVP(Camera3D &Camera);
void Resize(Camera3D &Camera, f32 NewAspect);
void Move(Camera3D &Camera);

namespace FlyBy {
    void Create(Camera3D &Camera);
    void Update(f32 DeltaTime);
}
