#pragma once

#include <cstdint>
#include "Engine/Types.hpp"

namespace InferusEngine {
    InferusResult Init();
    void Destroy();

    void Run();
    void Resize(uint32_t Width, uint32_t Height);
};
