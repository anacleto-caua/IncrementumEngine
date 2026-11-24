#pragma once

#include <string>
#include <vector>

#include "Core/Math.hpp"

// Loads static .obj meshes (plus their .mtl, if any) for instanced props. Faces are grouped into
// one SubMesh per material, so a typical sourced tree (bark material + alpha-cut leaf material)
// loads as-is with no atlas baking: each SubMesh carries its own diffuse texture path / color.
namespace ModelLoader {
    struct Vertex {
        vec3 Position;
        vec3 Normal;
        vec2 UV;  // v already flipped to top-left origin (stb_image row order); (0,0) when absent
    };

    struct SubMesh {
        u32 FirstIndex = 0;
        u32 IndexCount = 0;
        std::string DiffuseTexture;  // resolved path, empty = untextured
        std::string AlphaTexture;    // map_d, only when it differs from DiffuseTexture
        bool HasMaterial = false;    // false = no .mtl material at all - caller picks a fallback color
        vec3 DiffuseColor = vec3(1.0f);  // .mtl Kd (treated as linear, like Blender writes it)
    };

    struct Model {
        std::vector<Vertex> Vertices;
        std::vector<u32> Indices;  // grouped contiguously per SubMesh
        std::vector<SubMesh> SubMeshes;
    };

    // Parses one .obj file (tinyobjloader, libs/tinyobjloader) into an interleaved vertex/index
    // buffer ready for GPU upload. The .mtl and every texture path are resolved relative to the
    // .obj's own directory. Fails (IncResult::FAIL) on a missing/malformed file rather than
    // asserting - callers use INC_CHECK the same way as any other loader.
    IncResult LoadObj(const std::string& path, Model& out);

    // Rescales "model" uniformly so its bounding-box height equals "target_height", centers it on
    // X/Z and puts its lowest point at y = -sink * target_height (a small sink keeps roots from
    // floating on sloped ground). Sourced art arrives in arbitrary units, so this is what keeps a
    // swapped-in asset at a sane world size with no code change.
    void NormalizeHeight(Model& model, f32 target_height, f32 sink);
}
