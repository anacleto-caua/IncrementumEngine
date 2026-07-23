#include "Camera.hpp"

#include <imgui.h>
#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include "Utils/GlmDefaults.hpp"
#include "Engine/Core/Input.hpp"

Camera3D CreateCamera3D(f32 Aspect, f32 Fov) {
    Camera3D cam;
    cam.FocalLength = 1.0f / tan(glm::radians(Fov) / 2.0f);
    cam.Position = vec3::ZERO;
    cam.LookDir = vec3::FORWARD;
    cam.Model = glm::mat4(1.0f);
    cam.Projection = glm::perspective(glm::radians(Fov), Aspect, 0.1f, 1000.0f);
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
    Camera.View = glm::lookAt(Camera.Position, Camera.Position+Camera.LookDir, vec3::UP);
    RefreshMVP(Camera);
}

namespace FlyBy {
    static constexpr f32 SPEED = 20;
    static constexpr f32 RUNNING_MULT = 3;

    static constexpr f32 PITCH_SENSIBILITY = .1f;
    static constexpr f32 YAW_SENSIBILITY = .1f;

    static constexpr f32 PITCH_CLAMP_MIN = -89;
    static constexpr f32 PITCH_CLAMP_MAX = +89;

    static constexpr f32 YAW_CLAMP_MIN = 0;
    static constexpr f32 YAW_CLAMP_MAX = 360;

    glm::vec3 FrameMovement = vec3::ZERO;

    f32 Yaw = 0;
    f32 Pitch = 45;

    bool IsRunning = false;

    Camera3D* Camera;

    void ApplyRotation() {
        Camera->LookDir.x = glm::cos(glm::radians(Yaw)) * glm::cos(glm::radians(Pitch));
        Camera->LookDir.y = glm::sin(glm::radians(Pitch));
        Camera->LookDir.z = glm::sin(glm::radians(Yaw)) * glm::cos(glm::radians(Pitch));
        Camera->LookDir = glm::normalize(Camera->LookDir);
    }

    void Create(Camera3D &bCamera) {
        Camera = &bCamera;
        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Forward, [](void){ FrameMovement+=vec3::FORWARD; });
        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Backward, [](void){ FrameMovement-=vec3::FORWARD; });

        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Right, [](void){ FrameMovement+=vec3::RIGHT; });
        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Left, [](void){ FrameMovement-=vec3::RIGHT; });

        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Up, [](void){ FrameMovement+=vec3::UP; });
        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Down, [](void){ FrameMovement-=vec3::UP; });

        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Shift, Input::ActionType::Press, [](void){ IsRunning = true; });
        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Shift, Input::ActionType::Release, [](void){ IsRunning = false; });
        Update(0);
        ApplyRotation();
    }

    void Update(f32 DeltaTime) {
        bool IsDirty = false;

        {
            using namespace Input::Keyboard;

            if (IsKeyDown(Key::Forward)) {
                FrameMovement.z = 1;
            } else if (IsKeyDown(Key::Backward)) {
                FrameMovement.z = -1;
            }

            if (IsKeyDown(Key::Right)) {
                FrameMovement.x = 1;
            } else if (IsKeyDown(Key::Left)) {
                FrameMovement.x = -1;
            }

            if (IsKeyDown(Key::Up)) {
                FrameMovement.y = 1;
            } else if (IsKeyDown(Key::Down)) {
                FrameMovement.y = -1;
            }
        }

        if (FrameMovement != vec3::ZERO) {
            FrameMovement = glm::normalize(FrameMovement);

            glm::vec3 LocalFwd = Camera->LookDir;
            LocalFwd = glm::normalize(LocalFwd);

            glm::vec3 LocalRight = glm::normalize(glm::cross(vec3::UP, LocalFwd));

            glm::vec3 AllignedMovement =
                (LocalRight * FrameMovement.x) +
                (LocalFwd * FrameMovement.z) +
                (vec3::UP * FrameMovement.y);
            AllignedMovement = glm::normalize(AllignedMovement);

            Camera->Position += AllignedMovement * SPEED * DeltaTime * ( IsRunning ? RUNNING_MULT : 1 );
            FrameMovement = vec3::ZERO;
            IsDirty = true;
        }

        if (Input::Mouse::XDelta != 0 || Input::Mouse::YDelta != 0) {
            Pitch -= Input::Mouse::YDelta * PITCH_SENSIBILITY;
            Yaw -= Input::Mouse::XDelta * YAW_SENSIBILITY;

            if (Pitch < PITCH_CLAMP_MIN) {
                Pitch = PITCH_CLAMP_MIN;
            } else if (Pitch > PITCH_CLAMP_MAX) {
                Pitch = PITCH_CLAMP_MAX;
            }

            // It's more a wrap but well...
            if (Yaw < YAW_CLAMP_MIN) {
                Yaw += YAW_CLAMP_MAX;
            } else if (Yaw > YAW_CLAMP_MAX) {
                Yaw -= YAW_CLAMP_MIN;
            }

            ApplyRotation();
            IsDirty = true;
        }

        if (IsDirty) {
            Move(*Camera);
        }
    }
}
