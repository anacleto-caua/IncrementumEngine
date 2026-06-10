#pragma once

#include <vector>
#include <fstream>

namespace IO {
    static inline IncResult BinaryRead(const std::string &filename, std::vector<u32> &buffer, u32 &file_size_bytes) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            analog::critical("failed to open file: " + filename);
            return IncResult::FAIL;
        }

        file_size_bytes = (u32)file.tellg();

        u32 vector_size_words = (file_size_bytes + 3) / 4;
        buffer.resize(vector_size_words);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size_bytes);
        file.close();

        return IncResult::SUCCESS;
    }
}
