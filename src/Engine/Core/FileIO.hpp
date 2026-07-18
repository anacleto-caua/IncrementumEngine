#pragma once

#include <vector>

namespace FileIO {
    void Initialize();

    IncResult BinaryRead(const std::string &filename, std::vector<u32> &buffer, u32 &file_size_bytes);
}
