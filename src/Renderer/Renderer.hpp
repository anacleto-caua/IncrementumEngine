#pragma once

namespace Renderer {
    IncResult Create();
    void Destroy();

    void Frame();

    void Resize(i32 Width, i32 Height);
}
