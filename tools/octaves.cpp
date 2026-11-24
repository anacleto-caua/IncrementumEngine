// Standalone thesis-figure generator: three plain OpenSimplex2 octaves and their sum, as
// grayscale PNGs. Not part of the engine build - octaves.cmd compiles and runs it directly.
//
//   1_oitava.png  frequency f,  amplitude 1
//   2_oitava.png  frequency 2f, amplitude 0.5
//   3_oitava.png  frequency 4f, amplitude 0.25
//   4_soma.png    sum of the three, divided by the total amplitude (1.75)
//
// Every octave is drawn at its real amplitude (gray = 0.5 + 0.5 * noise * amplitude), never
// normalized on its own, so the shrinking contrast is what the figure shows.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <fnl/FastNoiseLite.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

constexpr int Size = 512;
constexpr int Seed = 1337;
constexpr int OctaveCount = 3;
// Per pixel. ~2-3 large blobs across the image at octave 1.
constexpr float BaseFrequency = 1.2f / static_cast<float>(Size);

static unsigned char ToGray(float value) {
    return static_cast<unsigned char>(std::lround(std::clamp(0.5f + 0.5f * value, 0.0f, 1.0f) * 255.0f));
}

static bool WritePng(const std::filesystem::path& path, const std::vector<unsigned char>& pixels) {
    return stbi_write_png(path.string().c_str(), Size, Size, 1, pixels.data(), Size) != 0;
}

int main(int argc, char** argv) {
    const std::filesystem::path out_dir = argc > 1 ? argv[1] : "imagens/oitavas";
    std::filesystem::create_directories(out_dir);

    std::vector<float> sum(static_cast<size_t>(Size) * Size, 0.0f);
    float total_amplitude = 0.0f;

    for (int octave = 0; octave < OctaveCount; ++octave) {
        const float frequency = BaseFrequency * static_cast<float>(1 << octave);
        const float amplitude = 1.0f / static_cast<float>(1 << octave);
        total_amplitude += amplitude;

        FastNoiseLite noise(Seed);
        noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        noise.SetFractalType(FastNoiseLite::FractalType_None);
        noise.SetFrequency(frequency);

        std::vector<unsigned char> pixels(sum.size());
        float min_value = 1e9f, max_value = -1e9f;
        for (int y = 0; y < Size; ++y) {
            for (int x = 0; x < Size; ++x) {
                const size_t i = static_cast<size_t>(y) * Size + static_cast<size_t>(x);
                const float value = noise.GetNoise(static_cast<float>(x), static_cast<float>(y)) * amplitude;
                sum[i] += value;
                pixels[i] = ToGray(value);
                min_value = std::min(min_value, value);
                max_value = std::max(max_value, value);
            }
        }

        const auto path = out_dir / (std::to_string(octave + 1) + "_oitava.png");
        if (!WritePng(path, pixels)) { std::fprintf(stderr, "failed to write %s\n", path.string().c_str()); return 1; }
        std::printf("%s  f=%.5f/px  amplitude=%.2f  range [%.3f, %.3f]\n", path.string().c_str(), frequency, amplitude, min_value, max_value);
    }

    std::vector<unsigned char> pixels(sum.size());
    float min_value = 1e9f, max_value = -1e9f;
    for (size_t i = 0; i < sum.size(); ++i) {
        const float value = sum[i] / total_amplitude;
        pixels[i] = ToGray(value);
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
    }
    const auto path = out_dir / "4_soma.png";
    if (!WritePng(path, pixels)) { std::fprintf(stderr, "failed to write %s\n", path.string().c_str()); return 1; }
    std::printf("%s  sum / %.2f  range [%.3f, %.3f]\n", path.string().c_str(), total_amplitude, min_value, max_value);
    return 0;
}
