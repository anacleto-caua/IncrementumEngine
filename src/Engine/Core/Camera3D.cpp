#include "Camera3D.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "Engine/Core/Input.hpp"
#include "glm/ext/matrix_clip_space.hpp"

// This class is most definetvelly overthinked with all this pointer stuff,
// I could most certainlly simply recalculate the matrix per frame.

void Camera3D::Init(float CurrAspect, glm::mat4* pModelViewProjection) {
    const constexpr glm::float32_t STARTING_POV   = 90.0;

    Position = { -10, 10, -10 };
    LookAt = Position + Vector3::FORWARD;
    Aspect = CurrAspect;

    ModelViewProjection = pModelViewProjection;

    FOV = STARTING_POV;
    FocalLength = 1.0f / tan(glm::radians(FOV) / 2.0f);

    Projection = glm::perspective(glm::radians(FOV), Aspect, 0.1f, 1000.0f);
    Projection[1][1] *= -1.0f; // Vulkan Y-flip

    Model = glm::mat4(1.0f);

    Move();
    Resize(CurrAspect);

    Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Forward, [this](void){ this->FrameMovement+=Vector3::FORWARD; });
    Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Backward, [this](void){ this->FrameMovement-=Vector3::FORWARD; });

    Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Right, [this](void){ this->FrameMovement+=Vector3::RIGHT; });
    Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Left, [this](void){ this->FrameMovement-=Vector3::RIGHT; });

    Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Up, [this](void){ this->FrameMovement+=Vector3::UP; });
    Input::Keyboard::RegisterCallback(Input::Keyboard::Key::Down, [this](void){ this->FrameMovement-=Vector3::UP; });

    Input::Keyboard::RegisterCallback(Input::ActionType::Press, Input::Keyboard::Key::Shift,
        [this](void){ this->IsRunning = true; });
    Input::Keyboard::RegisterCallback(Input::ActionType::Release, Input::Keyboard::Key::Shift,
        [this](void){ this->IsRunning = false; });
}

Camera3D::~Camera3D(){
    // ...
}

void Camera3D::Resize(float NewAspect) {
    Aspect = NewAspect;
    Projection[0][0] = FocalLength / NewAspect;
    RefreshMVP();
}

void Camera3D::Move() {
    View = glm::lookAtLH(Position, Position-LookDir, Vector3::UP);
    RefreshMVP();
}

void Camera3D::RefreshMVP() {
    *ModelViewProjection = Projection * View * Model;
}

void Camera3D::Update(float DeltaTime) {
    bool ShallMove = false;

    if (FrameMovement != Vector3::ZERO) {
        FrameMovement = glm::normalize(FrameMovement);

        glm::vec3 LocalFwd = LookDir;
        LocalFwd = glm::normalize(LocalFwd);

        glm::vec3 LocalRight = glm::normalize(glm::cross(Vector3::UP, LocalFwd));

        glm::vec3 AllignedMovement =
            (LocalRight * FrameMovement.x) +
            (LocalFwd * FrameMovement.z) +
            (Vector3::UP * FrameMovement.y);
        AllignedMovement = glm::normalize(AllignedMovement);

        Position -= AllignedMovement * SPEED * DeltaTime * ( IsRunning ? RUNNING_MULT : 1 );
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

        LookDir.x = glm::cos(glm::radians(Yaw)) * glm::cos(glm::radians(Pitch));
        LookDir.y = glm::sin(glm::radians(Pitch));
        LookDir.z = glm::sin(glm::radians(Yaw)) * glm::cos(glm::radians(Pitch));
        LookDir = glm::normalize(LookDir);
        ShallMove = true;
    }

    if (ShallMove) {
        Move();
    }
}
