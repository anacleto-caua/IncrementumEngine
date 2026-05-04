#pragma once

#include <vulkan/vulkan.h>

#define VK_CHECK(expr, msg)                                      \
    do {                                                         \
        if ((expr) != VK_SUCCESS) {                              \
                analog::error(msg);                              \
            }                                                    \
    } while(0)
