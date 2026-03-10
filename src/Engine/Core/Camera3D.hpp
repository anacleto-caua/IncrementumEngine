#pragma once

#include <glm/fwd.hpp>
#include <glm/ext/matrix_float4x4.hpp>

#include "Default.hpp"

class Camera3D {
public:
    static constexpr float SPEED = 20;
    static constexpr float RUNNING_MULT = 3;
    bool IsRunning = false;

    static constexpr float PITCH_SENSIBILITY = 10;
    static constexpr float YAW_SENSIBILITY = 15;

    static constexpr float PITCH_CLAMP_MIN = -89;
    static constexpr float PITCH_CLAMP_MAX = +89;

    static constexpr float YAW_CLAMP_MIN = 0;
    static constexpr float YAW_CLAMP_MAX = 360;

    float FOV;
    float Aspect;
    float FocalLength;

    glm::vec3 Position;
    glm::vec3 LookAt;

    glm::mat4 Model;
    glm::mat4 View;
    glm::mat4 Projection;

    glm::mat4* ModelViewProjection;

    glm::vec3 FrameMovement = Vector3::ZERO;

    float Yaw = 90;
    float Pitch = 0;
    glm::vec3 LookDir = Vector3::FORWARD;

public:
    Camera3D() = default;
    ~Camera3D();
    Camera3D(const Camera3D&) = delete;
    Camera3D& operator=(const Camera3D&) = delete;

    void Init(float CurrAspect, glm::mat4* pModelViewProjection);

    void Resize(float NewAspect);

    void Move();

    void RefreshMVP();

    void Update(float DeltaTime);
};
