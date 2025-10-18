#include "ModelImporter.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cstring>

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
}

bool IsSupportedExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".obj";
}

bool LoadOBJ(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage) {
    outMesh.Clear();
    errorMessage.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Failed to open OBJ file";
        return false;
    }

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
                    return false;
                }
                int vResolved = ResolveIndex(vRaw, positions.size());
                if (vResolved < 0 || static_cast<size_t>(vResolved) >= positions.size()) {
                    errorMessage = "Vertex index out of range at line " + std::to_string(lineNumber);
                    return false;
                }
                int nResolved = ResolveIndex(nRaw, normals.size());
                vIndices.push_back(vResolved);
                nIndices.push_back(nResolved);
            }

            if (vIndices.size() < 3) {
                errorMessage = "Face with fewer than 3 vertices at line " + std::to_string(lineNumber);
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
        return false;
    }

    if (hasBounds) {
        outMesh.bounds = AABB(minBounds, maxBounds);
    } else {
        outMesh.bounds = AABB();
    }

    return true;
}

} // namespace ModelImporter
