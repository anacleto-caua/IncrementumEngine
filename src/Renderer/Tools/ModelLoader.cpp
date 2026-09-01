#include "ModelLoader.hpp"

#include <map>

#include <tinyobjloader/tiny_obj_loader.h>

namespace ModelLoader {
    IncResult LoadObj(const std::string& path, Model& out) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());

        if (!warn.empty()) { analog::warn("ModelLoader: {} - {}", path, warn); }
        if (!ok) {
            analog::error("ModelLoader: failed to load {} - {}", path, err);
            return IncResult::FAIL;
        }

        out.Vertices.clear();
        out.Indices.clear();

        // Dedup by (vertex_index, normal_index): a face corner referencing the same position but
        // a different normal (flat-shaded faces sharing an edge) must become a distinct GPU vertex,
        // which is exactly what keying on the pair - not just vertex_index - gives for free.
        std::map<std::pair<int, int>, u32> seen;

        for (const tinyobj::shape_t& shape : shapes) {
            for (const tinyobj::index_t& index : shape.mesh.indices) {
                std::pair<int, int> key = { index.vertex_index, index.normal_index };

                auto found = seen.find(key);
                if (found != seen.end()) {
                    out.Indices.push_back(found->second);
                    continue;
                }

                Vertex vertex {};
                vertex.Position = {
                    attrib.vertices[static_cast<size_t>(3 * index.vertex_index) + 0],
                    attrib.vertices[static_cast<size_t>(3 * index.vertex_index) + 1],
                    attrib.vertices[static_cast<size_t>(3 * index.vertex_index) + 2]
                };

                if (index.normal_index >= 0) {
                    vertex.Normal = {
                        attrib.normals[static_cast<size_t>(3 * index.normal_index) + 0],
                        attrib.normals[static_cast<size_t>(3 * index.normal_index) + 1],
                        attrib.normals[static_cast<size_t>(3 * index.normal_index) + 2]
                    };
                } else {
                    analog::warn("ModelLoader: {} has a face with no normal - defaulting to +Y", path);
                    vertex.Normal = { 0.0f, 1.0f, 0.0f };
                }

                u32 new_index = static_cast<u32>(out.Vertices.size());
                out.Vertices.push_back(vertex);
                out.Indices.push_back(new_index);
                seen.emplace(key, new_index);
            }
        }

        return IncResult::SUCCESS;
    }
}
