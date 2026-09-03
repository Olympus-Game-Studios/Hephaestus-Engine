#include "ModelImporter.h"
#include "FbxStaticImporter.h"

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
#include <cstdint>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ModelImporter {
namespace {
    struct GLTFPrimitiveResult {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        int materialSlot = 0;
        std::string name;
    };

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

    bool AppendGLTFPrimitive(const json& primitive,
                             const json& accessors,
                             const json& bufferViews,
                             const std::vector<uint8_t>& binaryData,
                             MeshData& outMesh,
                             Vec3& minBounds,
                             Vec3& maxBounds,
                             bool& boundsInitialized,
                             std::string& errorMessage,
                             GLTFPrimitiveResult& outResult) {
        if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) {
            errorMessage = "Mesh primitive has no POSITION attribute";
            return false;
        }

        int posAccessorIdx = primitive["attributes"]["POSITION"];
        if (posAccessorIdx < 0 || posAccessorIdx >= static_cast<int>(accessors.size())) {
            errorMessage = "Invalid position accessor index";
            return false;
        }

        const auto& posAccessor = accessors[posAccessorIdx];
        int posBufferViewIdx = posAccessor["bufferView"];
        int posByteOffset = posAccessor.value("byteOffset", 0);
        int posCount = posAccessor["count"];

        if (posBufferViewIdx < 0 || posBufferViewIdx >= static_cast<int>(bufferViews.size())) {
            errorMessage = "Invalid position buffer view index";
            return false;
        }

        const auto& posBufferView = bufferViews[posBufferViewIdx];
        int posBufferViewOffset = posBufferView.value("byteOffset", 0);

        if (posBufferViewOffset < 0 || posByteOffset < 0) {
            errorMessage = "Invalid negative byte offset in position data";
            return false;
        }

        size_t posDataStart = static_cast<size_t>(posBufferViewOffset) + static_cast<size_t>(posByteOffset);
        size_t posDataSize = static_cast<size_t>(posCount) * 3 * sizeof(float);
        if (posDataStart + posDataSize > binaryData.size()) {
            errorMessage = "Position data exceeds binary buffer size";
            return false;
        }

        std::vector<Vec3> positions;
        positions.reserve(posCount);
        const float* posData = reinterpret_cast<const float*>(binaryData.data() + posDataStart);
        for (int i = 0; i < posCount; ++i) {
            Vec3 pos(posData[i * 3], posData[i * 3 + 1], posData[i * 3 + 2]);
            positions.push_back(pos);
            if (!boundsInitialized) {
                minBounds = pos;
                maxBounds = pos;
                boundsInitialized = true;
            } else {
                minBounds.x = std::min(minBounds.x, pos.x);
                minBounds.y = std::min(minBounds.y, pos.y);
                minBounds.z = std::min(minBounds.z, pos.z);
                maxBounds.x = std::max(maxBounds.x, pos.x);
                maxBounds.y = std::max(maxBounds.y, pos.y);
                maxBounds.z = std::max(maxBounds.z, pos.z);
            }
        }

        std::vector<uint32_t> indices;
        if (primitive.contains("indices")) {
            int indAccessorIdx = primitive["indices"];
            if (indAccessorIdx < 0 || indAccessorIdx >= static_cast<int>(accessors.size())) {
                errorMessage = "Invalid index accessor index";
                return false;
            }
            const auto& indAccessor = accessors[indAccessorIdx];
            int indBufferViewIdx = indAccessor["bufferView"];
            int indByteOffset = indAccessor.value("byteOffset", 0);
            int indCount = indAccessor["count"];

            if (indBufferViewIdx < 0 || indBufferViewIdx >= static_cast<int>(bufferViews.size())) {
                errorMessage = "Invalid index buffer view index";
                return false;
            }

            const auto& indBufferView = bufferViews[indBufferViewIdx];
            int indBufferViewOffset = indBufferView.value("byteOffset", 0);
            if (indBufferViewOffset < 0 || indByteOffset < 0) {
                errorMessage = "Invalid negative byte offset in index data";
                return false;
            }

            indices.reserve(indCount);
            size_t indDataStart = static_cast<size_t>(indBufferViewOffset) + static_cast<size_t>(indByteOffset);
            int indComponentType = indAccessor["componentType"];
            if (indComponentType == 5125) { // UNSIGNED_INT
                size_t indDataSize = static_cast<size_t>(indCount) * sizeof(uint32_t);
                if (indDataStart + indDataSize > binaryData.size()) {
                    errorMessage = "Index data exceeds binary buffer size";
                    return false;
                }
                const uint32_t* indData = reinterpret_cast<const uint32_t*>(binaryData.data() + indDataStart);
                for (int i = 0; i < indCount; ++i) {
                    indices.push_back(indData[i]);
                }
            } else if (indComponentType == 5123) { // UNSIGNED_SHORT
                size_t indDataSize = static_cast<size_t>(indCount) * sizeof(uint16_t);
                if (indDataStart + indDataSize > binaryData.size()) {
                    errorMessage = "Index data exceeds binary buffer size";
                    return false;
                }
                const uint16_t* indData = reinterpret_cast<const uint16_t*>(binaryData.data() + indDataStart);
                for (int i = 0; i < indCount; ++i) {
                    indices.push_back(indData[i]);
                }
            } else {
                errorMessage = "Unsupported index component type";
                return false;
            }
        } else {
            indices.reserve(positions.size());
            for (uint32_t i = 0; i < positions.size(); ++i) {
                indices.push_back(i);
            }
        }

        std::vector<Vec3> normals;
        if (primitive["attributes"].contains("NORMAL")) {
            int normAccessorIdx = primitive["attributes"]["NORMAL"];
            if (normAccessorIdx >= 0 && normAccessorIdx < static_cast<int>(accessors.size())) {
                const auto& normAccessor = accessors[normAccessorIdx];
                int normBufferViewIdx = normAccessor["bufferView"];
                int normByteOffset = normAccessor.value("byteOffset", 0);
                int normCount = normAccessor["count"];

                if (normBufferViewIdx >= 0 && normBufferViewIdx < static_cast<int>(bufferViews.size())) {
                    const auto& normBufferView = bufferViews[normBufferViewIdx];
                    int normBufferViewOffset = normBufferView.value("byteOffset", 0);
                    if (normBufferViewOffset >= 0 && normByteOffset >= 0) {
                        size_t normDataStart = static_cast<size_t>(normBufferViewOffset) + static_cast<size_t>(normByteOffset);
                        size_t normDataSize = static_cast<size_t>(normCount) * 3 * sizeof(float);
                        if (normDataStart + normDataSize <= binaryData.size()) {
                            const float* normData = reinterpret_cast<const float*>(binaryData.data() + normDataStart);
                            normals.reserve(normCount);
                            for (int i = 0; i < normCount; ++i) {
                                Vec3 normal(normData[i * 3], normData[i * 3 + 1], normData[i * 3 + 2]);
                                normals.push_back(normal);
                            }
                        } else {
                            std::cout << "[ModelImporter] Warning: Normal data exceeds buffer size, skipping normals" << std::endl;
                        }
                    }
                }
            }
        }

        uint32_t startIndex = static_cast<uint32_t>(outMesh.indices.size());
        for (uint32_t idx : indices) {
            if (idx >= positions.size()) {
                errorMessage = "Index out of range";
                return false;
            }

            Vertex vert{};
            Vec3 pos = positions[idx];
            vert.pos[0] = pos.x;
            vert.pos[1] = pos.y;
            vert.pos[2] = pos.z;

            if (idx < normals.size()) {
                Vec3 normal = normals[idx];
                vert.normal[0] = normal.x;
                vert.normal[1] = normal.y;
                vert.normal[2] = normal.z;
            } else {
                vert.normal[0] = 0.0f;
                vert.normal[1] = 1.0f;
                vert.normal[2] = 0.0f;
            }

            vert.color[0] = 1.0f;
            vert.color[1] = 1.0f;
            vert.color[2] = 1.0f;
            vert.texCoord[0] = 0.0f;
            vert.texCoord[1] = 0.0f;

            outMesh.vertices.push_back(vert);
            outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertices.size() - 1));
        }

        outResult.indexOffset = startIndex;
        outResult.indexCount = static_cast<uint32_t>(outMesh.indices.size() - startIndex);
        outResult.materialSlot = 0;
        if (primitive.contains("material") && primitive["material"].is_number_integer()) {
            outResult.materialSlot = primitive["material"].get<int>();
        }
        if (primitive.contains("name") && primitive["name"].is_string()) {
            outResult.name = primitive["name"].get<std::string>();
        }
        return true;
    }

    float ReadGLTFNumber(const json& value) {
        if (value.is_number_float()) {
            return value.get<float>();
        }
        if (value.is_number_integer()) {
            return static_cast<float>(value.get<std::int64_t>());
        }
        return 0.0f;
    }

    Mat4 Mat4FromGLTFQuaternion(float x, float y, float z, float w) {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
            w /= len;
        }

        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        const float xy = x * y;
        const float xz = x * z;
        const float yz = y * z;
        const float wx = w * x;
        const float wy = w * y;
        const float wz = w * z;

        Mat4 r = Mat4::Identity();
        r.m[0] = 1.0f - 2.0f * (yy + zz);
        r.m[1] = 2.0f * (xy + wz);
        r.m[2] = 2.0f * (xz - wy);
        r.m[4] = 2.0f * (xy - wz);
        r.m[5] = 1.0f - 2.0f * (xx + zz);
        r.m[6] = 2.0f * (yz + wx);
        r.m[8] = 2.0f * (xz + wy);
        r.m[9] = 2.0f * (yz - wx);
        r.m[10] = 1.0f - 2.0f * (xx + yy);
        return r;
    }

    Mat4 GLTFNodeLocalMatrix(const json& node) {
        if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
            Mat4 matrix = Mat4::Identity();
            for (int i = 0; i < 16; ++i) {
                matrix.m[i] = ReadGLTFNumber(node["matrix"][i]);
            }
            return matrix;
        }

        Vec3 translation(0.0f, 0.0f, 0.0f);
        Vec3 scale(1.0f, 1.0f, 1.0f);
        float qx = 0.0f;
        float qy = 0.0f;
        float qz = 0.0f;
        float qw = 1.0f;

        if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() >= 3) {
            translation.x = ReadGLTFNumber(node["translation"][0]);
            translation.y = ReadGLTFNumber(node["translation"][1]);
            translation.z = ReadGLTFNumber(node["translation"][2]);
        }
        if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
            scale.x = ReadGLTFNumber(node["scale"][0]);
            scale.y = ReadGLTFNumber(node["scale"][1]);
            scale.z = ReadGLTFNumber(node["scale"][2]);
        }
        if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
            qx = ReadGLTFNumber(node["rotation"][0]);
            qy = ReadGLTFNumber(node["rotation"][1]);
            qz = ReadGLTFNumber(node["rotation"][2]);
            qw = ReadGLTFNumber(node["rotation"][3]);
        }

        const Mat4 t = Mat4::Translation(translation);
        const Mat4 r = Mat4FromGLTFQuaternion(qx, qy, qz, qw);
        const Mat4 s = Mat4::Scale(scale);
        return Mat4::Multiply(t, Mat4::Multiply(r, s));
    }

    bool FindMeshWorldMatrixRecursive(const json& nodes,
                                      int nodeIndex,
                                      const Mat4& parentWorld,
                                      int meshIndex,
                                      Mat4& outWorld) {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) {
            return false;
        }

        const json& node = nodes[nodeIndex];
        const Mat4 world = Mat4::Multiply(parentWorld, GLTFNodeLocalMatrix(node));
        if (node.contains("mesh") && node["mesh"].is_number_integer() &&
            node["mesh"].get<int>() == meshIndex) {
            outWorld = world;
            return true;
        }

        if (node.contains("children") && node["children"].is_array()) {
            for (const auto& child : node["children"]) {
                if (!child.is_number_integer()) {
                    continue;
                }
                if (FindMeshWorldMatrixRecursive(nodes, child.get<int>(), world, meshIndex, outWorld)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool FindMeshWorldMatrix(const json& root, int meshIndex, Mat4& outWorld) {
        outWorld = Mat4::Identity();
        if (!root.contains("nodes") || !root["nodes"].is_array()) {
            return false;
        }

        const json& nodes = root["nodes"];
        if (root.contains("scenes") && root["scenes"].is_array() && !root["scenes"].empty()) {
            int sceneIndex = 0;
            if (root.contains("scene") && root["scene"].is_number_integer()) {
                sceneIndex = root["scene"].get<int>();
            }
            if (sceneIndex < 0 || sceneIndex >= static_cast<int>(root["scenes"].size())) {
                sceneIndex = 0;
            }
            const json& scene = root["scenes"][sceneIndex];
            if (scene.contains("nodes") && scene["nodes"].is_array()) {
                for (const auto& idx : scene["nodes"]) {
                    if (!idx.is_number_integer()) {
                        continue;
                    }
                    if (FindMeshWorldMatrixRecursive(nodes, idx.get<int>(), Mat4::Identity(),
                                                     meshIndex, outWorld)) {
                        return true;
                    }
                }
            }
        }

        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const json& node = nodes[i];
            if (node.contains("mesh") && node["mesh"].is_number_integer() &&
                node["mesh"].get<int>() == meshIndex) {
                outWorld = GLTFNodeLocalMatrix(node);
                return true;
            }
        }
        return false;
    }

    bool IsIdentityMatrix(const Mat4& matrix) {
        const Mat4 identity = Mat4::Identity();
        for (int i = 0; i < 16; ++i) {
            if (std::abs(matrix.m[i] - identity.m[i]) > 1e-6f) {
                return false;
            }
        }
        return true;
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

    void BakeNodeTransform(MeshData& mesh, const Mat4& world) {
        if (mesh.vertices.empty() || IsIdentityMatrix(world)) {
            return;
        }

        Vec3 minBounds{};
        Vec3 maxBounds{};
        bool boundsInitialized = false;
        for (Vertex& vert : mesh.vertices) {
            Vec3 pos(vert.pos[0], vert.pos[1], vert.pos[2]);
            Vec3 nrm(vert.normal[0], vert.normal[1], vert.normal[2]);
            pos = Mat4::TransformPoint(world, pos);
            nrm = TransformNormal(world, nrm);
            vert.pos[0] = pos.x;
            vert.pos[1] = pos.y;
            vert.pos[2] = pos.z;
            vert.normal[0] = nrm.x;
            vert.normal[1] = nrm.y;
            vert.normal[2] = nrm.z;
            if (!boundsInitialized) {
                minBounds = pos;
                maxBounds = pos;
                boundsInitialized = true;
            } else {
                minBounds.x = std::min(minBounds.x, pos.x);
                minBounds.y = std::min(minBounds.y, pos.y);
                minBounds.z = std::min(minBounds.z, pos.z);
                maxBounds.x = std::max(maxBounds.x, pos.x);
                maxBounds.y = std::max(maxBounds.y, pos.y);
                maxBounds.z = std::max(maxBounds.z, pos.z);
            }
        }
        if (boundsInitialized) {
            mesh.bounds = AABB(minBounds, maxBounds);
        }
    }

    bool LoadGLTFDocument(const json& root,
                          const std::vector<uint8_t>& binaryData,
                          MeshData& outMesh,
                          std::string& errorMessage) {
        if (!root.contains("meshes") || root["meshes"].empty()) {
            errorMessage = "No meshes found in glTF file";
            return false;
        }

        const auto& mesh = root["meshes"][0];
        if (!mesh.contains("primitives") || mesh["primitives"].empty()) {
            errorMessage = "No primitives found in mesh";
            return false;
        }

        if (!root.contains("accessors")) {
            errorMessage = "No accessors found in glTF file";
            return false;
        }

        const auto& accessors = root["accessors"];
        const auto& bufferViews = root.value("bufferViews", json::array());

        Vec3 minBounds{};
        Vec3 maxBounds{};
        bool boundsInitialized = false;
        outMesh.submeshes.clear();

        for (const auto& primitive : mesh["primitives"]) {
            GLTFPrimitiveResult primResult{};
            if (!AppendGLTFPrimitive(primitive, accessors, bufferViews, binaryData,
                                     outMesh, minBounds, maxBounds,
                                     boundsInitialized, errorMessage, primResult)) {
                return false;
            }
            if (primResult.indexCount > 0) {
                MeshData::Submesh submesh{};
                submesh.indexOffset = primResult.indexOffset;
                submesh.indexCount = primResult.indexCount;
                submesh.materialSlot = primResult.materialSlot;
                submesh.name = primResult.name;
                outMesh.submeshes.push_back(std::move(submesh));
            }
        }

        if (boundsInitialized) {
            outMesh.bounds = AABB(minBounds, maxBounds);
        } else {
            outMesh.bounds = AABB();
        }

        Mat4 world = Mat4::Identity();
        if (FindMeshWorldMatrix(root, 0, world)) {
            BakeNodeTransform(outMesh, world);
        }

        std::cout << "[ModelImporter] glTF load complete: verts=" << outMesh.vertices.size()
                  << ", indices=" << outMesh.indices.size()
                  << ", submeshes=" << outMesh.submeshes.size() << std::endl;
        for (size_t i = 0; i < outMesh.submeshes.size(); ++i) {
            const auto& sub = outMesh.submeshes[i];
            std::cout << "[ModelImporter]   submesh[" << i << "]: slot=" << sub.materialSlot
                      << " offset=" << sub.indexOffset << " count=" << sub.indexCount << std::endl;
        }
        return true;
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
    outMesh.Clear();
    errorMessage.clear();

    std::cout << "[ModelImporter] Begin loading glTF: " << path.string() << std::endl;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        errorMessage = "Failed to open glTF file";
        return false;
    }

    try {
        if (ext == ".glb") {
            // glTF Binary format: 12-byte header + JSON chunk + binary chunk
            uint32_t magic = 0;
            file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
            
            if (magic != 0x46546C67) { // "glTF" in little-endian
                errorMessage = "Invalid glTF binary file (wrong magic number)";
                return false;
            }

            uint32_t version = 0;
            uint32_t fileLength = 0;
            file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&fileLength), sizeof(uint32_t));

            if (version != 2) {
                errorMessage = "Only glTF 2.0 is supported";
                return false;
            }

            // Read JSON chunk header
            uint32_t chunkLength = 0;
            uint32_t chunkType = 0;
            file.read(reinterpret_cast<char*>(&chunkLength), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&chunkType), sizeof(uint32_t));

            if (chunkType != 0x4E4F534A) { // "JSON" in little-endian
                errorMessage = "Expected JSON chunk in glTF binary file";
                return false;
            }

            // Read JSON data
            std::string jsonContent(chunkLength, '\0');
            file.read(jsonContent.data(), chunkLength);

            // Read binary chunk header
            uint32_t binChunkLength = 0;
            uint32_t binChunkType = 0;
            file.read(reinterpret_cast<char*>(&binChunkLength), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&binChunkType), sizeof(uint32_t));

            if (binChunkType != 0x004E4942) { // "BIN\0" in little-endian
                errorMessage = "Expected BIN chunk in glTF binary file";
                return false;
            }

            // Read binary data
            std::vector<uint8_t> binaryData(binChunkLength);
            file.read(reinterpret_cast<char*>(binaryData.data()), binChunkLength);

            json root = json::parse(jsonContent);
            return LoadGLTFDocument(root, binaryData, outMesh, errorMessage);
        }
        else if (ext == ".gltf") {
            // For .gltf files, read as text and parse JSON
            std::string jsonContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            json root = json::parse(jsonContent);
            
            // Check if there are external buffers
            if (root.contains("buffers")) {
                std::vector<uint8_t> binaryData;
                
                for (const auto& buffer : root["buffers"]) {
                    if (buffer.contains("uri")) {
                        std::string uri = buffer["uri"];
                        
                        // Try to load external .bin file
                        std::filesystem::path binPath = path.parent_path() / uri;
                        std::ifstream binFile(binPath, std::ios::binary);
                        if (!binFile.is_open()) {
                            errorMessage = "Failed to open external binary file: " + uri;
                            return false;
                        }
                        
                        binFile.seekg(0, std::ios::end);
                        size_t binSize = binFile.tellg();
                        binFile.seekg(0, std::ios::beg);
                        
                        binaryData.resize(binSize);
                        binFile.read(reinterpret_cast<char*>(binaryData.data()), binSize);
                    }
                }
                
                if (binaryData.empty()) {
                    errorMessage = "No binary data found in glTF file";
                    return false;
                }

                return LoadGLTFDocument(root, binaryData, outMesh, errorMessage);
            }
            
            errorMessage = "No buffers found in glTF file";
            return false;
        }
        else {
            errorMessage = "Unsupported glTF file format";
            return false;
        }
    } catch (const std::exception& e) {
        errorMessage = std::string("Failed to load glTF file: ") + e.what();
        return false;
    }
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
