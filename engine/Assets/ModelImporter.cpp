#include "ModelImporter.h"
#include "FbxStaticImporter.h"
#include "GltfDocument.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <cstdlib>
#include <type_traits>
#include <array>
#include <map>
#include <functional>
#include <cstdint>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ModelImporter {
namespace {

    bool ParseIndexToken(const std::string& token, int& vOut, int& tOut, int& nOut) {
        vOut = 0;
        tOut = 0;
        nOut = 0;

        size_t firstSlash = token.find('/');
        size_t secondSlash = std::string::npos;
        if (firstSlash != std::string::npos) {
            secondSlash = token.find('/', firstSlash + 1);
        }

        auto parsePart = [](const std::string& part, bool& ok) -> int {
            if (part.empty()) {
                return 0;
            }
            try {
                return std::stoi(part);
            } catch (...) {
                ok = false;
                return 0;
            }
        };

        bool ok = true;
        if (firstSlash == std::string::npos) {
            vOut = parsePart(token, ok);
        } else {
            std::string vPart = token.substr(0, firstSlash);
            std::string tPart;
            std::string nPart;
            if (secondSlash == std::string::npos) {
                tPart = token.substr(firstSlash + 1);
            } else {
                tPart = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                nPart = token.substr(secondSlash + 1);
            }
            vOut = parsePart(vPart, ok);
            tOut = parsePart(tPart, ok);
            nOut = parsePart(nPart, ok);
        }
        return ok;
    }

    int ResolveIndex(int rawIndex, size_t count) {
        if (rawIndex > 0) {
            return rawIndex - 1;
        }
        if (rawIndex < 0) {
            return static_cast<int>(count) + rawIndex;
        }
        return -1;
    }

    bool StartsWith(const std::string& line, const char* prefix) {
        size_t idx = line.find_first_not_of(" \t\r\n");
        if (idx == std::string::npos) {
            return false;
        }
        size_t len = std::strlen(prefix);
        if (line.size() - idx < len) {
            return false;
        }
        return std::equal(prefix, prefix + len, line.begin() + static_cast<std::ptrdiff_t>(idx));
    }

    std::string TrimLeft(const std::string& line) {
        size_t idx = line.find_first_not_of(" \t\r\n");
        if (idx == std::string::npos) {
            return std::string();
        }
        return line.substr(idx);
    }

    float Determinant3(const Mat4& m) {
        return m.m[0] * (m.m[5] * m.m[10] - m.m[9] * m.m[6]) -
               m.m[4] * (m.m[1] * m.m[10] - m.m[9] * m.m[2]) +
               m.m[8] * (m.m[1] * m.m[6] - m.m[5] * m.m[2]);
    }

    Vec3 TransformNormal(const Mat4& world, const Vec3& normal) {
        const float a00 = world.m[0];
        const float a10 = world.m[1];
        const float a20 = world.m[2];
        const float a01 = world.m[4];
        const float a11 = world.m[5];
        const float a21 = world.m[6];
        const float a02 = world.m[8];
        const float a12 = world.m[9];
        const float a22 = world.m[10];

        const float c00 = a11 * a22 - a12 * a21;
        const float c01 = a12 * a20 - a10 * a22;
        const float c02 = a10 * a21 - a11 * a20;
        const float c10 = a02 * a21 - a01 * a22;
        const float c11 = a00 * a22 - a02 * a20;
        const float c12 = a01 * a20 - a00 * a21;
        const float c20 = a01 * a12 - a02 * a11;
        const float c21 = a02 * a10 - a00 * a12;
        const float c22 = a00 * a11 - a01 * a10;
        const float det = a00 * c00 + a01 * c01 + a02 * c02;
        if (std::abs(det) < 1e-12f) {
            return vnorm(Mat4::TransformDirection(world, normal));
        }
        const float invDet = 1.0f / det;
        const Vec3 transformed(
            (c00 * normal.x + c01 * normal.y + c02 * normal.z) * invDet,
            (c10 * normal.x + c11 * normal.y + c12 * normal.z) * invDet,
            (c20 * normal.x + c21 * normal.y + c22 * normal.z) * invDet);
        return vnorm(transformed);
    }

}

bool IsSupportedExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb";
}

bool LoadOBJ(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage) {
    outMesh.Clear();
    errorMessage.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Failed to open OBJ file";
        return false;
    }

    std::cout << "[ModelImporter] Begin loading OBJ: " << path.string() << std::endl;

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;

    std::string line;
    uint32_t lineNumber = 0;
    const float maxFloat = std::numeric_limits<float>::max();
    Vec3 minBounds(maxFloat, maxFloat, maxFloat);
    Vec3 maxBounds(-maxFloat, -maxFloat, -maxFloat);
    bool hasBounds = false;

    while (std::getline(file, line)) {
        ++lineNumber;
        std::string trimmed = TrimLeft(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (StartsWith(trimmed, "v ")) {
            std::istringstream iss(trimmed.substr(1));
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(iss >> x >> y >> z)) {
                errorMessage = "Invalid vertex definition at line " + std::to_string(lineNumber);
                std::cerr << "[ModelImporter] " << errorMessage << std::endl;
                return false;
            }
            positions.emplace_back(x, y, z);
            minBounds.x = std::min(minBounds.x, x);
            minBounds.y = std::min(minBounds.y, y);
            minBounds.z = std::min(minBounds.z, z);
            maxBounds.x = std::max(maxBounds.x, x);
            maxBounds.y = std::max(maxBounds.y, y);
            maxBounds.z = std::max(maxBounds.z, z);
            hasBounds = true;
            continue;
        }

        if (StartsWith(trimmed, "vt ")) {
            std::istringstream iss(trimmed.substr(2));
            float texU = 0.0f;
            float texV = 0.0f;
            if (!(iss >> texU >> texV)) {
                // Silently skip malformed texture coordinates
                continue;
            }
            texCoords.emplace_back(texU, texV);
            continue;
        }

        if (StartsWith(trimmed, "vn")) {
            std::istringstream iss(trimmed.substr(2));
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(iss >> x >> y >> z)) {
                errorMessage = "Invalid normal definition at line " + std::to_string(lineNumber);
                std::cerr << "[ModelImporter] " << errorMessage << std::endl;
                return false;
            }
            normals.emplace_back(x, y, z);
            continue;
        }

        if (StartsWith(trimmed, "f ")) {
            std::istringstream iss(trimmed.substr(1));
            std::vector<int> vIndices;
            std::vector<int> tIndices;
            std::vector<int> nIndices;
            std::string token;
            while (iss >> token) {
                int vRaw = 0;
                int tRaw = 0;
                int nRaw = 0;
                if (!ParseIndexToken(token, vRaw, tRaw, nRaw)) {
                    errorMessage = "Invalid face index at line " + std::to_string(lineNumber);
                    std::cerr << "[ModelImporter] " << errorMessage << std::endl;
                    return false;
                }
                int vResolved = ResolveIndex(vRaw, positions.size());
                if (vResolved < 0 || static_cast<size_t>(vResolved) >= positions.size()) {
                    errorMessage = "Vertex index out of range at line " + std::to_string(lineNumber);
                    std::cerr << "[ModelImporter] " << errorMessage << std::endl;
                    return false;
                }
                int nResolved = ResolveIndex(nRaw, normals.size());
                int tResolved = ResolveIndex(tRaw, texCoords.size());
                vIndices.push_back(vResolved);
                tIndices.push_back(tResolved);
                nIndices.push_back(nResolved);
            }

            if (vIndices.size() < 3) {
                errorMessage = "Face with fewer than 3 vertices at line " + std::to_string(lineNumber);
                std::cerr << "[ModelImporter] " << errorMessage << std::endl;
                return false;
            }

            for (size_t i = 1; i + 1 < vIndices.size(); ++i) {
                int tri[3] = { vIndices[0], vIndices[i], vIndices[i + 1] };
                int triNormalIdx[3] = { nIndices[0], nIndices[i], nIndices[i + 1] };
                int triTexIdx[3] = { tIndices[0], tIndices[i], tIndices[i + 1] };

                Vec3 p0 = positions[static_cast<size_t>(tri[0])];
                Vec3 p1 = positions[static_cast<size_t>(tri[1])];
                Vec3 p2 = positions[static_cast<size_t>(tri[2])];
                Vec3 fallbackNormal = vcross(vsub(p1, p0), vsub(p2, p0));
                if (vlen(fallbackNormal) < 1e-6f) {
                    fallbackNormal = Vec3(0.0f, 1.0f, 0.0f);
                } else {
                    fallbackNormal = vnorm(fallbackNormal);
                }

                for (int corner = 0; corner < 3; ++corner) {
                    Vertex vert{};
                    Vec3 pos = positions[static_cast<size_t>(tri[corner])];
                    vert.pos[0] = pos.x;
                    vert.pos[1] = pos.y;
                    vert.pos[2] = pos.z;

                    Vec3 normal = fallbackNormal;
                    int normalIdx = triNormalIdx[corner];
                    if (normalIdx >= 0 && static_cast<size_t>(normalIdx) < normals.size()) {
                        normal = vnorm(normals[static_cast<size_t>(normalIdx)]);
                    }
                    vert.normal[0] = normal.x;
                    vert.normal[1] = normal.y;
                    vert.normal[2] = normal.z;

                    vert.color[0] = 1.0f;
                    vert.color[1] = 1.0f;
                    vert.color[2] = 1.0f;

                    // Load texture coordinates
                    int texIdx = triTexIdx[corner];
                    if (texIdx >= 0 && static_cast<size_t>(texIdx) < texCoords.size()) {
                        vert.texCoord[0] = texCoords[static_cast<size_t>(texIdx)].x;
                        vert.texCoord[1] = texCoords[static_cast<size_t>(texIdx)].y;
                    } else {
                        vert.texCoord[0] = 0.0f;
                        vert.texCoord[1] = 0.0f;
                    }

                    outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertices.size()));
                    outMesh.vertices.push_back(vert);
                }
            }
            continue;
        }
    }

    if (outMesh.vertices.empty() || outMesh.indices.empty()) {
        errorMessage = "OBJ file contains no mesh data";
        std::cerr << "[ModelImporter] " << errorMessage << std::endl;
        return false;
    }

    if (hasBounds) {
        outMesh.bounds = AABB(minBounds, maxBounds);
    } else {
        outMesh.bounds = AABB();
    }

    std::cout << "[ModelImporter] OBJ load complete: verts=" << outMesh.vertices.size()
              << ", indices=" << outMesh.indices.size() << std::endl;

    return true;
}

bool LoadFBX(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage) {
    Hephaestus::FbxStaticImportResult meta;
    return Hephaestus::FbxStaticImporter::Load(
        path, Hephaestus::FbxSpaceConversion::Auto, outMesh, meta, errorMessage);
}

bool LoadGLTF(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage) {
    return LoadGLTF(path, outMesh, errorMessage, nullptr);
}

bool LoadGLTF(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage,
              std::vector<int>* outSlotMaterials) {
    using namespace Hephaestus::Gltf;
    outMesh.Clear();
    errorMessage.clear();
    if (outSlotMaterials) {
        outSlotMaterials->clear();
    }

    json root;
    std::vector<std::vector<uint8_t>> buffers;
    try {
        if (!LoadGlbOrGltf(path, root, buffers, errorMessage)) {
            return false;
        }
    } catch (const std::exception& e) {
        errorMessage = std::string("Failed to load glTF file: ") + e.what();
        return false;
    }

    const json nodes = root.value("nodes", json::array());
    const json meshes = root.value("meshes", json::array());
    const json materials = root.value("materials", json::array());
    if (meshes.empty()) {
        errorMessage = "No meshes found in glTF file";
        return false;
    }

    // One submesh per glTF material (slot == submesh index, as for FBX); -1 collects primitives without one.
    std::vector<int> slotMaterial;
    std::vector<std::vector<uint32_t>> slotIndices;
    auto slotFor = [&](int material) {
        const auto found = std::find(slotMaterial.begin(), slotMaterial.end(), material);
        if (found != slotMaterial.end()) {
            return static_cast<size_t>(found - slotMaterial.begin());
        }
        slotMaterial.push_back(material);
        slotIndices.emplace_back();
        return slotMaterial.size() - 1;
    };

    auto appendMesh = [&](int meshIndex, const Mat4& world) -> bool {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) {
            errorMessage = "glTF node references a missing mesh";
            return false;
        }
        const bool flipWinding = Determinant3(world) < 0.0f;
        // Primitives of one mesh often share their vertex accessors (one per material); load those vertices once.
        std::map<std::array<int, 3>, std::pair<uint32_t, int>> loadedVertices;
        for (const json& primitive : meshes[static_cast<size_t>(meshIndex)].value("primitives", json::array())) {
            if (primitive.value("mode", 4) != 4 || !primitive.contains("attributes") ||
                !primitive["attributes"].contains("POSITION")) {
                continue;
            }
            const json& attributes = primitive["attributes"];
            const std::array<int, 3> vertexKey = {attributes["POSITION"].get<int>(), attributes.value("NORMAL", -1),
                                                  attributes.value("TEXCOORD_0", -1)};
            uint32_t base = static_cast<uint32_t>(outMesh.vertices.size());
            int vertexCount = 0;
            if (const auto shared = loadedVertices.find(vertexKey); shared != loadedVertices.end()) {
                base = shared->second.first;
                vertexCount = shared->second.second;
            } else {
            AccessorView positions;
            if (!OpenAccessor(root, buffers, attributes["POSITION"].get<int>(), positions, errorMessage)) {
                return false;
            }
            if (positions.componentCount != 3) {
                errorMessage = "POSITION must be VEC3";
                return false;
            }
            std::vector<float> normals;
            std::vector<float> uvs;
            if (!ReadOptionalVecAttribute(root, buffers, attributes, "NORMAL", positions.count, 3, normals,
                                          errorMessage) ||
                !ReadOptionalVecAttribute(root, buffers, attributes, "TEXCOORD_0", positions.count, 2, uvs,
                                          errorMessage)) {
                return false;
            }

            for (int i = 0; i < positions.count; ++i) {
                Vertex vert{};
                const Vec3 pos = Mat4::TransformPoint(world, Vec3(ReadNormalized(positions, i, 0),
                                                                  ReadNormalized(positions, i, 1),
                                                                  ReadNormalized(positions, i, 2)));
                const Vec3 normal = normals.empty()
                    ? Vec3(0.0f, 1.0f, 0.0f)
                    : TransformNormal(world, Vec3(normals[static_cast<size_t>(i * 3)],
                                                  normals[static_cast<size_t>(i * 3 + 1)],
                                                  normals[static_cast<size_t>(i * 3 + 2)]));
                vert.pos[0] = pos.x;
                vert.pos[1] = pos.y;
                vert.pos[2] = pos.z;
                vert.normal[0] = normal.x;
                vert.normal[1] = normal.y;
                vert.normal[2] = normal.z;
                vert.color[0] = vert.color[1] = vert.color[2] = 1.0f;
                if (!uvs.empty()) {
                    vert.texCoord[0] = uvs[static_cast<size_t>(i * 2)];
                    vert.texCoord[1] = uvs[static_cast<size_t>(i * 2 + 1)];
                }
                outMesh.vertices.push_back(vert);
            }
            vertexCount = positions.count;
            loadedVertices.emplace(vertexKey, std::make_pair(base, vertexCount));
            }

            std::vector<uint32_t> local;
            if (primitive.contains("indices")) {
                AccessorView indexView;
                if (!OpenAccessor(root, buffers, primitive["indices"].get<int>(), indexView, errorMessage)) {
                    return false;
                }
                local.reserve(static_cast<size_t>(indexView.count));
                for (int i = 0; i < indexView.count; ++i) {
                    const auto index = static_cast<uint32_t>(ReadNormalized(indexView, i, 0));
                    if (index >= static_cast<uint32_t>(vertexCount)) {
                        errorMessage = "Index out of range";
                        return false;
                    }
                    local.push_back(index);
                }
            } else {
                for (int i = 0; i < vertexCount; ++i) {
                    local.push_back(static_cast<uint32_t>(i));
                }
            }
            std::vector<uint32_t>& target = slotIndices[slotFor(primitive.value("material", -1))];
            for (size_t i = 0; i + 2 < local.size(); i += 3) {
                target.push_back(base + local[i]);
                target.push_back(base + local[flipWinding ? i + 2 : i + 1]);
                target.push_back(base + local[flipWinding ? i + 1 : i + 2]);
            }
        }
        return true;
    };

    std::function<bool(int, const Mat4&, int)> visit = [&](int nodeIndex, const Mat4& parent, int depth) {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()) || depth > 256) {
            return true;
        }
        const json& node = nodes[static_cast<size_t>(nodeIndex)];
        const Mat4 world = Mat4::Multiply(parent, GLTFNodeLocalMatrix(node));
        if (node.contains("mesh") && node["mesh"].is_number_integer() &&
            !appendMesh(node["mesh"].get<int>(), world)) {
            return false;
        }
        for (const json& child : node.value("children", json::array())) {
            if (child.is_number_integer() && !visit(child.get<int>(), world, depth + 1)) {
                return false;
            }
        }
        return true;
    };

    const json scenes = root.value("scenes", json::array());
    const int sceneIndex = root.value("scene", 0);
    if (sceneIndex >= 0 && sceneIndex < static_cast<int>(scenes.size())) {
        for (const json& node : scenes[static_cast<size_t>(sceneIndex)].value("nodes", json::array())) {
            if (node.is_number_integer() && !visit(node.get<int>(), Mat4::Identity(), 0)) {
                return false;
            }
        }
    } else if (!appendMesh(0, Mat4::Identity())) {
        return false;
    }

    for (size_t slot = 0; slot < slotIndices.size(); ++slot) {
        if (slotIndices[slot].empty()) {
            continue;
        }
        MeshData::Submesh submesh;
        submesh.indexOffset = static_cast<uint32_t>(outMesh.indices.size());
        submesh.indexCount = static_cast<uint32_t>(slotIndices[slot].size());
        submesh.materialSlot = static_cast<int>(outMesh.submeshes.size());
        const int material = slotMaterial[slot];
        submesh.name = material >= 0 && material < static_cast<int>(materials.size())
            ? materials[static_cast<size_t>(material)].value("name", "Material " + std::to_string(material))
            : std::string("Default");
        outMesh.indices.insert(outMesh.indices.end(), slotIndices[slot].begin(), slotIndices[slot].end());
        outMesh.submeshes.push_back(std::move(submesh));
        if (outSlotMaterials) {
            outSlotMaterials->push_back(material);
        }
    }
    if (outMesh.indices.empty()) {
        errorMessage = "glTF file has no triangle geometry";
        return false;
    }

    const float maxFloat = std::numeric_limits<float>::max();
    auto boundsOf = [&](uint32_t offset, uint32_t count) {
        Vec3 minB(maxFloat, maxFloat, maxFloat);
        Vec3 maxB(-maxFloat, -maxFloat, -maxFloat);
        for (uint32_t i = 0; i < count; ++i) {
            const Vertex& vert = outMesh.vertices[outMesh.indices[offset + i]];
            minB = Vec3(std::min(minB.x, vert.pos[0]), std::min(minB.y, vert.pos[1]), std::min(minB.z, vert.pos[2]));
            maxB = Vec3(std::max(maxB.x, vert.pos[0]), std::max(maxB.y, vert.pos[1]), std::max(maxB.z, vert.pos[2]));
        }
        return AABB(minB, maxB);
    };
    for (MeshData::Submesh& submesh : outMesh.submeshes) {
        submesh.bounds = boundsOf(submesh.indexOffset, submesh.indexCount);
    }
    outMesh.bounds = boundsOf(0, static_cast<uint32_t>(outMesh.indices.size()));
    return true;
}

bool LoadModel(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".obj") {
        return LoadOBJ(path, outMesh, errorMessage);
    }

    if (ext == ".fbx") {
        return LoadFBX(path, outMesh, errorMessage);
    }

    if (ext == ".gltf" || ext == ".glb") {
        return LoadGLTF(path, outMesh, errorMessage);
    }

    errorMessage = "Unsupported model format";
    return false;
}

} // namespace ModelImporter
