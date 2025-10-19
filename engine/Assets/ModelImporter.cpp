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

namespace ModelImporter {
namespace {
    struct FBXArrays {
        std::vector<double> vertices;
        std::vector<int> indices;
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
}

bool IsSupportedExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".obj" || ext == ".fbx";
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
                vIndices.push_back(vResolved);
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

            outMesh.indices.push_back(static_cast<uint32_t>(outMesh.vertices.size()));
            outMesh.vertices.push_back(vert);
        }
    }

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

    errorMessage = "Unsupported model format";
    return false;
}

} // namespace ModelImporter
