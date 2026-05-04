#pragma once

#include <vulkan/vulkan.h>

#define VK_CHECK(expr, ...)                                             \
    do {                                                                \
        if ((expr) != VK_SUCCESS) {                                     \
                analog::error(__VA_ARGS__);                             \
                analog::error("vulkan function returned: {}", expr);    \
                return IncResult::FAIL;                                 \
            }                                                           \
    } while(0)
