#pragma once

#include "Core/Math.hpp"

struct Camera3D {
    f32 FocalLength;
    vec3 Position;
    vec3 LookDir;
    mat4 Model;
    mat4 View;
    mat4 Projection;
    mat4 ModelViewProjection;
};

Camera3D CreateCamera3D(f32 Aspect, f32 Fov);
void RefreshMVP(Camera3D &Camera);
void Resize(Camera3D &Camera, f32 NewAspect);
void Move(Camera3D &Camera);

namespace FlyBy {
    void Create(Camera3D &Camera);
    void Update(f32 DeltaTime);
}
