#pragma once

#include "Camera.hpp"

namespace Renderer {
    inline Camera* CurrentCamera;

    IncResult Create();
    void Destroy();

    void Frame();

    void Resize(i32 width, i32 height);
    void BindCamera(Camera* camera);
}
