#pragma once

#include <vector>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace IO {
    static inline void BinaryRead(const std::string &filename, std::vector<char> &buffer, uint32_t &file_size) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        file_size = (uint32_t)file.tellg();
        buffer.reserve(file_size);

        file.seekg(0);
        file.read(buffer.data(), file_size);
        file.close();
    }
}
