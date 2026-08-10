#include "FileIO.hpp"

#include <vector>
#include <fstream>
#include <filesystem>

#include <stb/stb_image.h>

namespace fs = std::filesystem;

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>
#endif

namespace FileIO {
    fs::path EngineRootPath;

    fs::path GetExecutablePath() {
#if defined(_WIN32)
        // Windows: Use the Win32 API
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);

        return fs::path(buffer);

#elif defined(__linux__)
        // Linux: Read the special symlink that always points to the current executable
        char buffer[PATH_MAX];
        i64 count = readlink("/proc/self/exe", buffer, PATH_MAX);

        if (count != -1) {
            return fs::path(std::string(buffer, static_cast<u64>(count)));
        }

        analog::warn("Sub-optimal situation reached, given root folder may be wrong.");

        return fs::path();
#else
        analog::critical("Unsupported platform for path resolution.");
        return fs::path();
#endif
    }


    void Initialize() {

        fs::path exe_path = GetExecutablePath();

        EngineRootPath = exe_path.parent_path();

        /**
         * Example usage:
         *      fs::path texture = EngineRootPath / "assets" / "textures" / "hero.png";
         *      analog::info("Loading texture from: {}", texture.string());
         */
    }

    IncResult BinaryRead(const std::string &filename, std::vector<u32> &buffer, u32 &file_size_bytes) {
        fs::path file_path = EngineRootPath / filename;
        std::ifstream file(file_path, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            analog::critical("failed to open file: {} ", filename);
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

    IncResult ImageRead(const std::string &filename, std::vector<u8> &buffer, u32 &width, u32 &height, u32 &channels, i32 desired_channels) {
        fs::path file_path = EngineRootPath / filename;

        i32 w, h, c;
        // stb_image allocates its own memory for the pixels.
        unsigned char* pixels = stbi_load(file_path.string().c_str(), &w, &h, &c, desired_channels);

        if (!pixels) {
            analog::critical("failed to load image: {}", filename);
            return IncResult::FAIL;
        }

        // Set the output variables
        width = (u32)w;
        height = (u32)h;
        channels = desired_channels > 0 ? (u32)desired_channels : (u32)c;

        u32 image_size_bytes = width * height * channels;

        // Dump the bytes into the output vector to maintain the same memory ownership
        buffer.assign(pixels, pixels + image_size_bytes);

        //  Ideally I could pass just the pointer and request
        //  the api user to free by himself using stbi_image_free(ptr) later
        stbi_image_free(pixels);

        return IncResult::SUCCESS;
    }
}
