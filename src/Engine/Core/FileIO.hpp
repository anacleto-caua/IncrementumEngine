#pragma once

#include <vector>

namespace FileIO {
    void Initialize();

    IncResult BinaryRead(const std::string &filename, std::vector<u32> &buffer, u32 &file_size_bytes);

    IncResult ImageRead(const std::string &filename, std::vector<u8> &buffer, u32 &width, u32 &height, u32 &channels, i32 desired_channels = 0);
}
