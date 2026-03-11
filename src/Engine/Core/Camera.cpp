#include "Camera.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include "Default.hpp"
#include "Engine/Core/Input.hpp"

namespace Camera {
    Camera3D CreateCamera3D(float Aspect, float Fov, glm::mat4* MVP) {
        Camera3D cam;
        cam.FocalLength = 1.0f / tan(glm::radians(Fov) / 2.0f);
        cam.Position = Vector3::ZERO;
        cam.LookDir = Vector3::FORWARD;
        cam.Model = glm::mat4(1.0f);
        cam.Projection = glm::perspective(glm::radians(Fov), Aspect, 0.1f, 1000.0f);
        cam.Projection[1][1] *= -1.0f; // Vulkan Y-flip
        cam.ModelViewProjection = MVP;
        return cam;
    }

    void RefreshMVP(Camera3D &Camera) {
        *Camera.ModelViewProjection = Camera.Projection * Camera.View * Camera.Model;
    }

    void Resize(Camera3D &Camera, float NewAspect) {
        Camera.Projection[0][0] = Camera.FocalLength / NewAspect;
        RefreshMVP(Camera);
    }

    void Move(Camera3D &Camera) {
        Camera.View = glm::lookAt(Camera.Position, Camera.Position-Camera.LookDir, Vector3::UP);
        RefreshMVP(Camera);
    }

    namespace FlyBy {
        static constexpr float SPEED = 20;
        static constexpr float RUNNING_MULT = 3;

        static constexpr float PITCH_SENSIBILITY = 10;
        static constexpr float YAW_SENSIBILITY = 15;

        static constexpr float PITCH_CLAMP_MIN = -89;
        static constexpr float PITCH_CLAMP_MAX = +89;

        static constexpr float YAW_CLAMP_MIN = 0;
        static constexpr float YAW_CLAMP_MAX = 360;

        glm::vec3 FrameMovement = Vector3::ZERO;

        float Yaw = 90;
        float Pitch = 0;

        bool IsRunning = false;

        Camera3D* Camera;

        void Create(Camera3D &bCamera) {
            Camera = &bCamera;
            Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Forward, [](void){ FrameMovement+=Vector3::FORWARD; });
            Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Backward, [](void){ FrameMovement-=Vector3::FORWARD; });

            Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Right, [](void){ FrameMovement+=Vector3::RIGHT; });
            Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Left, [](void){ FrameMovement-=Vector3::RIGHT; });

            Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Up, [](void){ FrameMovement+=Vector3::UP; });
            Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Down, [](void){ FrameMovement-=Vector3::UP; });

            Input::Keyboard::RegisterCallback(Input::ActionType::Press, Input::Keyboard::Key::Shift, [](void){ IsRunning = true; });
            Input::Keyboard::RegisterCallback(Input::ActionType::Release, Input::Keyboard::Key::Shift, [](void){ IsRunning = false; });
        }

        void Update(float DeltaTime) {
            bool ShallMove = false;

            if (FrameMovement != Vector3::ZERO) {
                FrameMovement = glm::normalize(FrameMovement);

                glm::vec3 LocalFwd = Camera->LookDir;
                LocalFwd = glm::normalize(LocalFwd);

                glm::vec3 LocalRight = glm::normalize(glm::cross(Vector3::UP, LocalFwd));

                glm::vec3 AllignedMovement =
                    (LocalRight * FrameMovement.x) +
                    (LocalFwd * FrameMovement.z) +
                    (Vector3::UP * FrameMovement.y);
                AllignedMovement = glm::normalize(AllignedMovement);

                Camera->Position -= AllignedMovement * SPEED * DeltaTime * ( IsRunning ? RUNNING_MULT : 1 );
                FrameMovement = Vector3::ZERO;
                ShallMove = true;
            }

            if (Input::Mouse::XDelta != 0 || Input::Mouse::YDelta != 0) {
                Pitch -= Input::Mouse::YDelta * PITCH_SENSIBILITY * DeltaTime;
                Yaw -= Input::Mouse::XDelta * YAW_SENSIBILITY * DeltaTime;

                if (Pitch < PITCH_CLAMP_MIN) {
                    Pitch = PITCH_CLAMP_MIN;
                } else if (Pitch > PITCH_CLAMP_MAX) {
                    Pitch = PITCH_CLAMP_MAX;
                }

                // It's more a wrap but well...
                if (Yaw < YAW_CLAMP_MIN) {
                    Yaw = YAW_CLAMP_MAX;
                } else if (Yaw > YAW_CLAMP_MAX) {
                    Yaw = YAW_CLAMP_MIN;
                }

                Camera->LookDir.x = glm::cos(glm::radians(Yaw)) * glm::cos(glm::radians(Pitch));
                Camera->LookDir.y = glm::sin(glm::radians(Pitch));
                Camera->LookDir.z = glm::sin(glm::radians(Yaw)) * glm::cos(glm::radians(Pitch));
                Camera->LookDir = glm::normalize(Camera->LookDir);
                ShallMove = true;
            }

            if (ShallMove) {
                Move(*Camera);
            }
        }

        glm::vec3* GetPositionPointer() {
            return &Camera->Position;
        }

    }
}
