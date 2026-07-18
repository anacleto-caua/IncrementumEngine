#include "FileIO.hpp"

#include <vector>
#include <fstream>
#include <filesystem>

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
        // Linux: Read the special symlink that always points to the current executable - TODO: non tested
        char buffer[PATH_MAX];
        u64 count = readlink("/proc/self/exe", buffer, PATH_MAX);

        if (count != -1) {
            return fs::path(std::string(buffer, count));
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
}
