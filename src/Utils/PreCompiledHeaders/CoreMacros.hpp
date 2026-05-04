#pragma once

#include "Types.hpp"
#include "Utils/AnaLog/analog.hpp"

#define INC_CHECK(expr, ...)                            \
    do {                                                \
        if ((expr) != IncResult::SUCCESS) {             \
            analog::error(__VA_ARGS__);                 \
        }                                               \
    } while(0)
