/**
 * @file FbxStaticImporter.cpp
 * @brief Triangulate FBX mesh instances into MeshData with baked node transforms.
 */

#include "FbxStaticImporter.h"

#include "FbxDocument.h"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Hephaestus {
namespace {

float MatrixDeterminant3(const Mat4& m) {
    const float a00 = m.m[0], a10 = m.m[1], a20 = m.m[2];
    const float a01 = m.m[4], a11 = m.m[5], a21 = m.m[6];
    const float a02 = m.m[8], a12 = m.m[9], a22 = m.m[10];
    return a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
           a02 * (a10 * a21 - a11 * a20);
}

Vec3 TransformPoint(const Mat4& m, const Vec3& p) {
    const Vec4 t = Mat4::TransformVec4(m, Vec4(p.x, p.y, p.z, 1.0f));
    return Vec3(t.x, t.y, t.z);
}

Vec3 TransformDirection(const Mat4& m, const Vec3& d) {
    const Vec4 t = Mat4::TransformVec4(m, Vec4(d.x, d.y, d.z, 0.0f));
    return vnorm(Vec3(t.x, t.y, t.z));
}

void ExpandBounds(Vec3& minB, Vec3& maxB, const Vec3& p) {
    minB.x = std::min(minB.x, p.x);
    minB.y = std::min(minB.y, p.y);
    minB.z = std::min(minB.z, p.z);
    maxB.x = std::max(maxB.x, p.x);
    maxB.y = std::max(maxB.y, p.y);
    maxB.z = std::max(maxB.z, p.z);
}

bool AppendMeshInstance(ufbx_node* node,
                        MeshData& outMesh,
                        Vec3& minB,
                        Vec3& maxB,
                        std::vector<std::string>& warnings,
                        bool& warnedExtraUv) {
    ufbx_mesh* mesh = node->mesh;
    if (!mesh || !mesh->vertex_position.exists || mesh->num_faces == 0) {
        return true;
    }

    const Mat4 world = FbxDocument::ToMat4(node->geometry_to_world);
    const bool flipWinding = MatrixDeterminant3(world) < 0.0f;
    if (mesh->uv_sets.count > 1 && !warnedExtraUv) {
        warnings.push_back("Additional UV channels were ignored. Only UV0 is imported.");
        warnedExtraUv = true;
    }

    const size_t partCount = mesh->material_parts.count > 0 ? mesh->material_parts.count : 1;
    std::vector<uint32_t> tri(std::max<size_t>(mesh->max_face_triangles * 3, 3));

    for (size_t partIx = 0; partIx < partCount; ++partIx) {
        const ufbx_mesh_part* part =
            mesh->material_parts.count > 0 ? &mesh->material_parts.data[partIx] : nullptr;
        if (part && part->num_triangles == 0 && part->num_faces == 0) {
            continue;
        }

        MeshData::Submesh submesh;
        submesh.indexOffset = static_cast<uint32_t>(outMesh.indices.size());
        submesh.materialSlot = static_cast<int>(outMesh.submeshes.size());

        std::string slotName;
        ufbx_material* material = nullptr;
        if (node->materials.count > partIx) {
            material = node->materials.data[partIx];
        } else if (mesh->materials.count > partIx) {
            material = mesh->materials.data[partIx];
        }
        if (material) {
            slotName = FbxDocument::ToString(material->name);
        }
        if (slotName.empty()) {
            slotName = FbxDocument::ToString(mesh->name);
        }
        if (slotName.empty()) {
            slotName = FbxDocument::ToString(node->name);
        }
        if (slotName.empty()) {
            slotName = "Submesh";
        }
        submesh.name = slotName;

        Vec3 subMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
        Vec3 subMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max());

        const size_t faceCount = part ? part->num_faces : mesh->faces.count;
        for (size_t fi = 0; fi < faceCount; ++fi) {
            const uint32_t faceIndex =
                part ? part->face_indices.data[fi] : static_cast<uint32_t>(fi);
            const ufbx_face face = mesh->faces.data[faceIndex];
            if (face.num_indices < 3) {
                continue;
            }
            const uint32_t triCount =
                ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            if (triCount == 0) {
                continue;
            }
            for (uint32_t t = 0; t < triCount; ++t) {
                uint32_t corners[3] = {tri[t * 3 + 0], tri[t * 3 + 1], tri[t * 3 + 2]};
                if (flipWinding) {
                    std::swap(corners[1], corners[2]);
                }
                for (uint32_t c = 0; c < 3; ++c) {
                    const uint32_t index = corners[c];
                    Vertex vert{};
                    const ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                    const Vec3 worldPos = TransformPoint(world, FbxDocument::ToVec3(pos));
                    vert.pos[0] = worldPos.x;
                    vert.pos[1] = worldPos.y;
                    vert.pos[2] = worldPos.z;

                    Vec3 normal(0.0f, 1.0f, 0.0f);
                    if (mesh->vertex_normal.exists) {
                        normal = TransformDirection(
                            world, FbxDocument::ToVec3(
                                       ufbx_get_vertex_vec3(&mesh->vertex_normal, index)));
                    }
                    vert.normal[0] = normal.x;
                    vert.normal[1] = normal.y;
                    vert.normal[2] = normal.z;

                    vert.color[0] = 1.0f;
                    vert.color[1] = 1.0f;
                    vert.color[2] = 1.0f;
                    if (mesh->vertex_color.exists) {
                        const ufbx_vec4 col = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
                        vert.color[0] = static_cast<float>(col.x);
                        vert.color[1] = static_cast<float>(col.y);
                        vert.color[2] = static_cast<float>(col.z);
                    }

                    vert.texCoord[0] = 0.0f;
                    vert.texCoord[1] = 0.0f;
                    if (mesh->vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                        vert.texCoord[0] = static_cast<float>(uv.x);
                        vert.texCoord[1] = 1.0f - static_cast<float>(uv.y); // FBX V points up, image rows start at the top
                    }

                    ExpandBounds(minB, maxB, worldPos);
                    ExpandBounds(subMin, subMax, worldPos);
                    outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertices.size()));
                    outMesh.vertices.push_back(vert);
                }
            }
        }

        submesh.indexCount =
            static_cast<uint32_t>(outMesh.indices.size()) - submesh.indexOffset;
        if (submesh.indexCount == 0) {
            continue;
        }
        submesh.bounds = AABB(subMin, subMax);
        outMesh.submeshes.push_back(std::move(submesh));
    }
    return true;
}

} // namespace

bool FbxStaticImporter::Load(const std::filesystem::path& path,
                             FbxSpaceConversion spaceConversion,
                             MeshData& outMesh,
                             FbxStaticImportResult& outMeta,
                             std::string& outError) {
    outMesh.Clear();
    outMeta = {};
    outError.clear();

    FbxDocument document;
    FbxLoadSettings settings;
    settings.spaceConversion = spaceConversion;
    settings.skeletalMesh = false;
    if (!document.Load(path, settings, outError)) {
        return false;
    }

    ufbx_scene* scene = document.Scene();
    outMeta.containsSkin = document.ContainsSkin();
    if (outMeta.containsSkin) {
        outMeta.warnings.push_back(
            "This FBX contains a skinned mesh. Skinning and animation were ignored. "
            "Use Import Character… to import it as a character.");
    }
    if (document.ContainsBlendShapes()) {
        outMeta.warnings.push_back("Blend shapes were ignored.");
    }

    Vec3 minB(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max());
    Vec3 maxB(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max());
    bool warnedExtraUv = false;
    bool anyMesh = false;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        ufbx_node* node = scene->nodes.data[i];
        if (!node || !node->mesh) {
            continue;
        }
        anyMesh = true;
        AppendMeshInstance(node, outMesh, minB, maxB, outMeta.warnings, warnedExtraUv);
    }

    if (!anyMesh || outMesh.vertices.empty() || outMesh.indices.empty()) {
        outError = "FBX file contains no mesh.";
        outMesh.Clear();
        return false;
    }

    outMesh.bounds = AABB(minB, maxB);
    for (size_t i = 0; i < scene->textures.count; ++i) {
        const ufbx_texture* texture = scene->textures.data[i];
        if (!texture) {
            continue;
        }
        const std::string abs = FbxDocument::ToString(texture->absolute_filename);
        const std::string rel = FbxDocument::ToString(texture->relative_filename);
        if (!abs.empty() && (abs.find(":\\") != std::string::npos || abs.find(":/") != std::string::npos)) {
            outMeta.warnings.push_back(
                "Texture uses an absolute path and was not copied into the project: " + abs);
        }
        if (texture->content.size > 0) {
            const std::string hint = rel.empty() ? FbxDocument::ToString(texture->filename) : rel;
            if (FbxDocument::DetectImageExtension(texture->content, hint).empty()) {
                outMeta.warnings.push_back(
                    "Embedded texture format is unknown; extraction skipped"
                    + (hint.empty() ? std::string(".") : (": " + hint)));
            }
        }
    }
    return true;
}

} // namespace Hephaestus
