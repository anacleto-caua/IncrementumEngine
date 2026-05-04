#include "Renderer.hpp"

namespace Renderer {
    IncResult Create() {
        return IncResult::SUCCESS;
    }

    void Destroy() {
        // ...
    }

    void Frame() {
        // ...
    }

    void Resize([[maybe_unused]]u32 Width, [[maybe_unused]]u32 Height) {
        // ...
    }
}
