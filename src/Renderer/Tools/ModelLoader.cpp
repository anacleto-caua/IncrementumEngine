#include "ModelLoader.hpp"

#include <map>
#include <tuple>
#include <limits>
#include <filesystem>

#include <tinyobjloader/tiny_obj_loader.h>

namespace ModelLoader {
    namespace {
        std::string ResolveTexturePath(const std::filesystem::path& base_dir, const std::string& name) {
            if (name.empty()) { return {}; }
            std::filesystem::path texture_path(name);
            if (texture_path.is_relative()) { texture_path = base_dir / texture_path; }
            return texture_path.lexically_normal().generic_string();
        }
    }

    IncResult LoadObj(const std::string& path, Model& out) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;

        std::filesystem::path base_dir = std::filesystem::path(path).parent_path();
        std::string mtl_dir = base_dir.empty() ? std::string("./") : base_dir.generic_string() + "/";

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), mtl_dir.c_str());

        if (!warn.empty()) { analog::warn("ModelLoader: {} - {}", path, warn); }
        if (!ok) {
            analog::error("ModelLoader: failed to load {} - {}", path, err);
            return IncResult::FAIL;
        }

        out.Vertices.clear();
        out.Indices.clear();
        out.SubMeshes.clear();

        // Bucket every face corner by material first (material -1 = "no material"), so each
        // SubMesh ends up as one contiguous index range - one draw per SubMesh, not per face.
        std::map<int, std::vector<tinyobj::index_t>> corners_by_material;
        for (const tinyobj::shape_t& shape : shapes) {
            size_t face_count = shape.mesh.num_face_vertices.size();
            size_t corner = 0;
            for (size_t face = 0; face < face_count; face++) {
                size_t face_vertices = shape.mesh.num_face_vertices[face];
                int material = face < shape.mesh.material_ids.size() ? shape.mesh.material_ids[face] : -1;
                std::vector<tinyobj::index_t>& bucket = corners_by_material[material];
                for (size_t v = 0; v < face_vertices; v++) { bucket.push_back(shape.mesh.indices[corner + v]); }
                corner += face_vertices;
            }
        }

        // Dedup by (vertex, normal, texcoord): a corner sharing a position but differing in normal
        // (flat-shaded edge) or UV (texture seam) must become a distinct GPU vertex.
        std::map<std::tuple<int, int, int>, u32> seen;
        bool warned_missing_normal = false;

        for (const auto& [material, corners] : corners_by_material) {
            SubMesh sub_mesh {};
            sub_mesh.FirstIndex = static_cast<u32>(out.Indices.size());

            if (material >= 0 && static_cast<size_t>(material) < materials.size()) {
                const tinyobj::material_t& mtl = materials[static_cast<size_t>(material)];
                sub_mesh.HasMaterial = true;
                sub_mesh.DiffuseColor = { mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2] };
                sub_mesh.DiffuseTexture = ResolveTexturePath(base_dir, mtl.diffuse_texname);
                std::string alpha_texture = ResolveTexturePath(base_dir, mtl.alpha_texname);
                if (alpha_texture != sub_mesh.DiffuseTexture) { sub_mesh.AlphaTexture = alpha_texture; }

                // tinyobj defaults a missing Kd to black, and Blender 4.x omits Kd entirely when
                // the base color is texture-linked - a textured material with Kd 0 would only
                // ever render black, so read it as "no tint" instead.
                bool kd_is_black = sub_mesh.DiffuseColor.x == 0.0f && sub_mesh.DiffuseColor.y == 0.0f && sub_mesh.DiffuseColor.z == 0.0f;
                if (!sub_mesh.DiffuseTexture.empty() && kd_is_black) { sub_mesh.DiffuseColor = vec3(1.0f); }
            }

            for (const tinyobj::index_t& index : corners) {
                std::tuple<int, int, int> key = { index.vertex_index, index.normal_index, index.texcoord_index };

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
                    if (!warned_missing_normal) {
                        analog::warn("ModelLoader: {} has faces with no normal - defaulting them to +Y", path);
                        warned_missing_normal = true;
                    }
                    vertex.Normal = { 0.0f, 1.0f, 0.0f };
                }

                if (index.texcoord_index >= 0) {
                    // OBJ's v origin is bottom-left; images are loaded top row first.
                    vertex.UV = {
                        attrib.texcoords[static_cast<size_t>(2 * index.texcoord_index) + 0],
                        1.0f - attrib.texcoords[static_cast<size_t>(2 * index.texcoord_index) + 1]
                    };
                } else {
                    vertex.UV = { 0.0f, 0.0f };
                }

                u32 new_index = static_cast<u32>(out.Vertices.size());
                out.Vertices.push_back(vertex);
                out.Indices.push_back(new_index);
                seen.emplace(key, new_index);
            }

            sub_mesh.IndexCount = static_cast<u32>(out.Indices.size()) - sub_mesh.FirstIndex;
            if (sub_mesh.IndexCount > 0) { out.SubMeshes.push_back(sub_mesh); }
        }

        if (out.Indices.empty()) {
            analog::error("ModelLoader: {} contains no faces", path);
            return IncResult::FAIL;
        }

        return IncResult::SUCCESS;
    }

    void NormalizeHeight(Model& model, f32 target_height, f32 sink) {
        vec3 min_corner(std::numeric_limits<f32>::max());
        vec3 max_corner(std::numeric_limits<f32>::lowest());
        for (const Vertex& vertex : model.Vertices) {
            min_corner = glm::min(glm::vec3(min_corner), glm::vec3(vertex.Position));
            max_corner = glm::max(glm::vec3(max_corner), glm::vec3(vertex.Position));
        }

        f32 height = max_corner.y - min_corner.y;
        if (height <= 0.0f) { return; }

        f32 scale = target_height / height;
        vec3 pivot = { (min_corner.x + max_corner.x) * 0.5f, min_corner.y, (min_corner.z + max_corner.z) * 0.5f };
        f32 sink_offset = sink * target_height;

        for (Vertex& vertex : model.Vertices) {
            vec3 local = vertex.Position - pivot;
            vertex.Position = { local.x * scale, local.y * scale - sink_offset, local.z * scale };
        }
    }
}
