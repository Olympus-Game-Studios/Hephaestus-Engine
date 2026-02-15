#include "ModelImporter.h"

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
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ModelImporter {
namespace {
    struct FBXArrays {
        std::vector<double> vertices;
        std::vector<int> indices;
    };

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

    bool ExtractFBXArraySegment(const std::string& content, const std::string& key, std::string& outSegment) {
        size_t keyPos = content.find(key);
        if (keyPos == std::string::npos) {
            return false;
        }

        size_t arrayPos = content.find("a:", keyPos);
        if (arrayPos == std::string::npos) {
            return false;
        }
        arrayPos += 2;

        size_t start = content.find_first_not_of(" \t\r\n", arrayPos);
        if (start == std::string::npos) {
            return false;
        }

        size_t end = content.find('}', start);
        if (end == std::string::npos) {
            return false;
        }

        outSegment = content.substr(start, end - start);
        return true;
    }

    template<typename T>
    bool ParseFBXArrayValues(const std::string& segment, std::vector<T>& outValues) {
        const char* ptr = segment.c_str();
        const char* end = ptr + segment.size();

        while (ptr < end) {
            while (ptr < end && (std::isspace(static_cast<unsigned char>(*ptr)) || *ptr == ',' || *ptr == ';')) {
                ++ptr;
            }
            if (ptr >= end) {
                break;
            }

            char* next = nullptr;
            if constexpr (std::is_same_v<T, double>) {
                double value = std::strtod(ptr, &next);
                if (next == ptr) {
                    break;
                }
                outValues.push_back(value);
            } else {
                long value = std::strtol(ptr, &next, 10);
                if (next == ptr) {
                    break;
                }
                outValues.push_back(static_cast<T>(value));
            }
            ptr = next;
        }

        return !outValues.empty();
    }

    bool ParseFBXArrays(const std::string& content, FBXArrays& arrays, std::string& errorMessage) {
        std::string verticesSegment;
        if (!ExtractFBXArraySegment(content, "Vertices", verticesSegment)) {
            errorMessage = "Vertices array not found in FBX file";
            return false;
        }

        if (!ParseFBXArrayValues(verticesSegment, arrays.vertices)) {
            errorMessage = "Failed to parse FBX vertex array";
            return false;
        }

        std::string indicesSegment;
        if (!ExtractFBXArraySegment(content, "PolygonVertexIndex", indicesSegment)) {
            errorMessage = "PolygonVertexIndex array not found in FBX file";
            return false;
        }

        if (!ParseFBXArrayValues(indicesSegment, arrays.indices)) {
            errorMessage = "Failed to parse FBX index array";
            return false;
        }

        return true;
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
    outMesh.Clear();
    errorMessage.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        errorMessage = "Failed to open FBX file";
        return false;
    }

    std::string content;
    file.seekg(0, std::ios::end);
    content.resize(static_cast<size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    if (!content.empty()) {
        file.read(content.data(), static_cast<std::streamsize>(content.size()));
    }
    file.close();

    if (content.empty()) {
        errorMessage = "FBX file is empty";
        return false;
    }

    if (content.size() >= 21 && content.compare(0, 21, "Kaydara FBX Binary") == 0) {
        errorMessage = "Binary FBX files are not supported yet. Please export as ASCII.";
        return false;
    }

    FBXArrays arrays;
    if (!ParseFBXArrays(content, arrays, errorMessage)) {
        return false;
    }

    if (arrays.vertices.size() % 3 != 0) {
        errorMessage = "FBX vertex array is not divisible by 3";
        return false;
    }

    size_t positionCount = arrays.vertices.size() / 3;
    if (positionCount == 0) {
        errorMessage = "FBX file contains no vertices";
        return false;
    }

    std::vector<Vec3> positions(positionCount);
    const float maxFloat = std::numeric_limits<float>::max();
    Vec3 minBounds(maxFloat, maxFloat, maxFloat);
    Vec3 maxBounds(-maxFloat, -maxFloat, -maxFloat);

    for (size_t i = 0; i < positionCount; ++i) {
        double x = arrays.vertices[i * 3 + 0];
        double y = arrays.vertices[i * 3 + 1];
        double z = arrays.vertices[i * 3 + 2];
        Vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        positions[i] = pos;

        minBounds.x = std::min(minBounds.x, pos.x);
        minBounds.y = std::min(minBounds.y, pos.y);
        minBounds.z = std::min(minBounds.z, pos.z);

        maxBounds.x = std::max(maxBounds.x, pos.x);
        maxBounds.y = std::max(maxBounds.y, pos.y);
        maxBounds.z = std::max(maxBounds.z, pos.z);
    }

    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(arrays.indices.size() / 3);
    std::vector<int> polygon;
    polygon.reserve(8);

    for (int rawIndex : arrays.indices) {
        bool endOfPolygon = rawIndex < 0;
        int actualIndex = endOfPolygon ? (-rawIndex - 1) : rawIndex;

        if (actualIndex < 0 || static_cast<size_t>(actualIndex) >= positions.size()) {
            errorMessage = "FBX index references out-of-range vertex";
            return false;
        }

        polygon.push_back(actualIndex);

        if (endOfPolygon) {
            if (polygon.size() >= 3) {
                for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                    triangles.push_back({ polygon[0], polygon[i], polygon[i + 1] });
                }
            }
            polygon.clear();
        }
    }

    if (!polygon.empty()) {
        if (polygon.size() >= 3) {
            for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                triangles.push_back({ polygon[0], polygon[i], polygon[i + 1] });
            }
        }
        polygon.clear();
    }

    if (triangles.empty()) {
        errorMessage = "FBX file contains no polygon data";
        return false;
    }

    outMesh.bounds = AABB(minBounds, maxBounds);

    for (const auto& tri : triangles) {
        Vec3 p0 = positions[static_cast<size_t>(tri[0])];
        Vec3 p1 = positions[static_cast<size_t>(tri[1])];
        Vec3 p2 = positions[static_cast<size_t>(tri[2])];

        Vec3 normal = vcross(vsub(p1, p0), vsub(p2, p0));
        if (vlen(normal) < 1e-6f) {
            normal = Vec3(0.0f, 1.0f, 0.0f);
        } else {
            normal = vnorm(normal);
        }

        const int triangleIndices[3] = { tri[0], tri[1], tri[2] };
        for (int idx : triangleIndices) {
            Vertex vert{};
            Vec3 pos = positions[static_cast<size_t>(idx)];
            vert.pos[0] = pos.x;
            vert.pos[1] = pos.y;
            vert.pos[2] = pos.z;

            vert.normal[0] = normal.x;
            vert.normal[1] = normal.y;
            vert.normal[2] = normal.z;

            vert.color[0] = 1.0f;
            vert.color[1] = 1.0f;
            vert.color[2] = 1.0f;

            // Default texture coordinates for FBX (no UV data available yet)
            vert.texCoord[0] = 0.0f;
            vert.texCoord[1] = 0.0f;

            outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertices.size()));
            outMesh.vertices.push_back(vert);
        }
    }

    return true;
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
            
            // Extract mesh data
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
                
                // Same parsing logic as .glb
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

                return true;
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
