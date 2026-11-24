#pragma once

#include <vulkan/vulkan.h>

#include "Core/JsonWriter.hpp"

// Hardware/OS/driver description for benchmark records - what the TCC's machine table needs,
// gathered from OS APIs and the live Vulkan device rather than filled in by hand. Call after
// Renderer::Init() (the GPU half reads VkVault).
namespace SystemInfo {
    // Writes fields (no enclosing braces) into the object the caller has open.
    void WriteMachineFields(JsonWriter& json);

    const char* PresentModeName(VkPresentModeKHR mode);
}
