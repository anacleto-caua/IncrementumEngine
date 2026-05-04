#pragma once

namespace Renderer {
    IncResult Create();
    void Destroy();

    void Frame();

    void Resize(u32 Width, u32 Height);
}
