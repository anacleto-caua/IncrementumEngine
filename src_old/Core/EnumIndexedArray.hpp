#pragma once

#include <array>

template <typename T, typename EnumT, EnumT Count>
struct EnumIndexedArray {
    std::array<T, static_cast<u64>(Count)> data{};

    constexpr T& operator[](EnumT index) noexcept {
        return data[static_cast<u64>(index)];
    }

    constexpr const T& operator[](EnumT index) const noexcept {
        return data[static_cast<u64>(index)];
    }

    constexpr auto begin() noexcept {
        return data.begin();
    }

    constexpr auto end() noexcept {
        return data.end();
    }

    constexpr auto begin() const noexcept {
        return data.begin();
    }

    constexpr auto end() const noexcept {
        return data.end();
    }
};
