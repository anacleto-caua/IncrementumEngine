#include "Renderer.hpp"

#include "Vk.hpp"

namespace Renderer {
    IncResult Create() {
        INC_CHECK(VulkanContext::Create(), "vulkan context creation failed");

        return IncResult::SUCCESS;
    }

    void Destroy() {
        VulkanContext::Destroy();
    }

    void Frame() {
        // ...
    }

    void Resize([[maybe_unused]]i32 Width, [[maybe_unused]]i32 Height) {
        // ...
    }
}
