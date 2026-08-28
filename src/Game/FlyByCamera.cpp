#include "FlyByCamera.hpp"

#include "Core/Math.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/InputContext.hpp"

namespace FlyByCamera {
    static constexpr f32 SPEED = 20;
    f32 RunningMultplier = 1.0f;

    static constexpr f32 PITCH_SENSIBILITY = .1f;
    static constexpr f32 YAW_SENSIBILITY = .1f;

    static constexpr f32 PITCH_CLAMP_MIN = -89;
    static constexpr f32 PITCH_CLAMP_MAX = +89;

    static constexpr f32 YAW_CLAMP_MIN = 0;
    static constexpr f32 YAW_CLAMP_MAX = 360;

    enum class Action {
        MoveForward, MoveBackward,
        MoveRight, MoveLeft,
        MoveUp, MoveDown,
        Run,

        _COUNT_
    };

    InputContext<Action, Action::_COUNT_> Context;

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

        Context.BindKey(Action::MoveForward, Input::Key::W);
        Context.BindKey(Action::MoveBackward, Input::Key::S);
        Context.BindKey(Action::MoveRight, Input::Key::D);
        Context.BindKey(Action::MoveLeft, Input::Key::A);
        Context.BindKey(Action::MoveUp, Input::Key::Q);
        Context.BindKey(Action::MoveDown, Input::Key::E);
        Context.BindKey(Action::Run, Input::Key::LShift);

        Context.OnPressed(Action::Run, [](){ IsRunning = true; });
        Context.OnReleased(Action::Run, [](){ IsRunning = false; });

        Update(0);
        ApplyRotation();
        UpdateMatrices(*TrackedCamera);
    }

    void Update(f32 delta_time) {
        Context.Frame();

        bool IsDirty = false;

        if (Context.IsActionDown(Action::MoveForward)) {
            FrameMovement.z = 1;
        } else if (Context.IsActionDown(Action::MoveBackward)) {
            FrameMovement.z = -1;
        }

        if (Context.IsActionDown(Action::MoveRight)) {
            FrameMovement.x = 1;
        } else if (Context.IsActionDown(Action::MoveLeft)) {
            FrameMovement.x = -1;
        }

        if (Context.IsActionDown(Action::MoveUp)) {
            FrameMovement.y = 1;
        } else if (Context.IsActionDown(Action::MoveDown)) {
            FrameMovement.y = -1;
        }

        RunningMultplier += Input::MouseScrollY;
        if (RunningMultplier <= 0.0f) {
            RunningMultplier = 0.1f;
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

            TrackedCamera->Position += AllignedMovement * SPEED * delta_time * ( IsRunning ? RunningMultplier : 1 );
            FrameMovement = vec3::ZERO;
            IsDirty = true;
        }

        if (Input::MouseXDelta != 0 || Input::MouseYDelta != 0) {
            Pitch -= Input::MouseYDelta * PITCH_SENSIBILITY;
            Yaw -= Input::MouseXDelta * YAW_SENSIBILITY;

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
