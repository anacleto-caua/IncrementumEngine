#pragma once

#include <glm/fwd.hpp>
#include <glm/ext/matrix_float4x4.hpp>

namespace Camera {
    struct Camera3D {
        float FocalLength;
        glm::vec3 Position;
        glm::vec3 LookDir;
        glm::mat4 Model;
        glm::mat4 View;
        glm::mat4 Projection;
        glm::mat4* ModelViewProjection;
    };

    Camera3D CreateCamera3D(float Aspect, float Fov, glm::mat4* MVP);
    void Resize(Camera3D &Camera, float NewAspect);
    void Move(Camera3D &Camera);

    namespace FlyBy {
        void Create(Camera3D &Camera);
        void Update(float DeltaTime);
        glm::vec3* GetPositionPointer();
    }
}
