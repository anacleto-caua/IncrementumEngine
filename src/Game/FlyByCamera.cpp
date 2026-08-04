#include "FlyByCamera.hpp"

#include "Core/Math.hpp"
#include "Engine/Core/Input.hpp"

namespace FlyByCamera {
    static constexpr f32 SPEED = 20;
    static constexpr f32 RUNNING_MULT = 3;

    static constexpr f32 PITCH_SENSIBILITY = .1f;
    static constexpr f32 YAW_SENSIBILITY = .1f;

    static constexpr f32 PITCH_CLAMP_MIN = -89;
    static constexpr f32 PITCH_CLAMP_MAX = +89;

    static constexpr f32 YAW_CLAMP_MIN = 0;
    static constexpr f32 YAW_CLAMP_MAX = 360;

    vec3 FrameMovement = vec3::ZERO;

    f32 Yaw = 0;
    f32 Pitch = -45;

    bool IsRunning = false;

    Camera* TrackedCamera;

    void ApplyRotation() {
        TrackedCamera->Front.x = cos(math::radians(Yaw)) * cos(math::radians(Pitch));
        TrackedCamera->Front.y = sin(math::radians(Pitch));
        TrackedCamera->Front.z = sin(math::radians(Yaw)) * cos(math::radians(Pitch));
        TrackedCamera->Front = normalize(TrackedCamera->Front);
    }

    void Bind(Camera &new_camera) {
        TrackedCamera = &new_camera;

        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Shift, Input::ActionType::Press, [](void){ IsRunning = true; });
        Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Shift, Input::ActionType::Release, [](void){ IsRunning = false; });

        Update(0);
        ApplyRotation();
        UpdateMatrices(*TrackedCamera);
    }

    void Update(f32 delta_time) {
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
            FrameMovement = normalize(FrameMovement);

            vec3 LocalFwd = TrackedCamera->Front;
            LocalFwd = normalize(LocalFwd);

            vec3 LocalRight = normalize(cross(vec3::UP, LocalFwd));

            vec3 AllignedMovement =
                (LocalRight * FrameMovement.x) +
                (LocalFwd * FrameMovement.z) +
                (vec3::UP * FrameMovement.y);
            AllignedMovement = normalize(AllignedMovement);

            TrackedCamera->Position += AllignedMovement * SPEED * delta_time * ( IsRunning ? RUNNING_MULT : 1 );
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
            UpdateMatrices(*TrackedCamera);
        }
    }
}
