#pragma once

#include <string>
#include <vector>

#include "Core/Math.hpp"

// Loads static, single-material-less .obj meshes for instanced props. Deliberately position+
// normal only, no UV - v1 props are flat/vertex-shaded (no textures); add UV back here (and to
// PropPass's vertex input state) the day a textured prop needs one.
namespace ModelLoader {
    struct Vertex {
        vec3 Position;
        vec3 Normal;
    };

    struct Model {
        std::vector<Vertex> Vertices;
        std::vector<u32> Indices;
    };

    // Parses one .obj file (tinyobjloader, libs/tinyobjloader) into an interleaved vertex/index
    // buffer ready for GPU upload. Fails (IncResult::FAIL) on a missing/malformed file rather than
    // asserting - callers use INC_CHECK the same way as any other loader.
    IncResult LoadObj(const std::string& path, Model& out);
}
