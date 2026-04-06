#pragma once

#include "Types.hpp"
#include "Utils/AnaLog/analog.hpp"

// The do { ... } while(0) is a standard C++ trick to make the macro
// require a semicolon when used, keeping auto-formatting happy.
#define INC_CHECK(expr, ret_val, ...)                            \
    do {                                                         \
        if ((expr) != IncResult::SUCCESS) {                  \
            analog::error(__VA_ARGS__);                             \
            return (ret_val);                                    \
        }                                                        \
    } while(0)

/*
#define VK_CHECK(expr, msg)                                      \
    do {                                                         \
        if ((expr) != VK_SUCCESS) {                              \
            analog::error(msg);                                  \
            }                                                    \
    } while(0
*/
