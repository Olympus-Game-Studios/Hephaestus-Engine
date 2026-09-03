/**
 * @file ModelImporterTests.cpp
 * @brief Headless glTF import checks: node TRS baking and builtin mesh orientation.
 */

#include "Assets/ModelImporter.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void PadTo4(std::vector<uint8_t>& out, uint8_t pad) {
    while (out.size() % 4 != 0) {
        out.push_back(pad);
    }
}

bool WriteTriangleGlb(const fs::path& path, const char* nodeRotationJson) {
    // Positions: (1,0,0), (0,1,0), (0,0,0). 90° X maps (0,1,0) -> (0,0,1).
    const float positions[] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
    };
    const float normals[] = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
    };
    const uint16_t indices[] = {0, 1, 2};

    std::vector<uint8_t> bin;
    AppendBytes(bin, positions, sizeof(positions));
    AppendBytes(bin, normals, sizeof(normals));
    AppendBytes(bin, indices, sizeof(indices));
    PadTo4(bin, 0);

    std::string json =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
                    "\"scenes\":[{\"nodes\":[0]}],"
                    "\"nodes\":[{\"mesh\":0,\"rotation\":") +
        nodeRotationJson +
        "}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},"
        "\"indices\":2}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}"
        "],"
        "\"buffers\":[{\"byteLength\":" +
        std::to_string(bin.size()) + "}]}";

    std::vector<uint8_t> jsonBytes(json.begin(), json.end());
    PadTo4(jsonBytes, static_cast<uint8_t>(' '));

    std::vector<uint8_t> file;
    const uint32_t magic = 0x46546C67;
    const uint32_t version = 2;
    const uint32_t jsonLen = static_cast<uint32_t>(jsonBytes.size());
    const uint32_t jsonType = 0x4E4F534A;
    const uint32_t binLen = static_cast<uint32_t>(bin.size());
    const uint32_t binType = 0x004E4942;
    const uint32_t length = 12 + 8 + jsonLen + 8 + binLen;
    AppendBytes(file, &magic, 4);
    AppendBytes(file, &version, 4);
    AppendBytes(file, &length, 4);
    AppendBytes(file, &jsonLen, 4);
    AppendBytes(file, &jsonType, 4);
    file.insert(file.end(), jsonBytes.begin(), jsonBytes.end());
    AppendBytes(file, &binLen, 4);
    AppendBytes(file, &binType, 4);
    file.insert(file.end(), bin.begin(), bin.end());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

bool AlmostEqual(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) <= eps;
}

bool ContainsApprox(const MeshData& mesh, float x, float y, float z) {
    for (const Vertex& vert : mesh.vertices) {
        if (AlmostEqual(vert.pos[0], x) && AlmostEqual(vert.pos[1], y) &&
            AlmostEqual(vert.pos[2], z)) {
            return true;
        }
    }
    return false;
}

float ExtentX(const AABB& b) { return b.max.x - b.min.x; }
float ExtentY(const AABB& b) { return b.max.y - b.min.y; }
float ExtentZ(const AABB& b) { return b.max.z - b.min.z; }

bool TestNodeRotationBake(const fs::path& scratch) {
    bool ok = true;
    const fs::path path = scratch / "rotated_triangle.glb";
    // glTF +90° X: (x, y, z, w) = (0.70710678, 0, 0, 0.70710678)
    if (!WriteTriangleGlb(path, "[0.70710678,0,0,0.70710678]")) {
        return Check(false, "write rotated triangle glb");
    }

    MeshData mesh;
    std::string error;
    ok &= Check(ModelImporter::LoadGLTF(path, mesh, error), "load rotated triangle: " + error);
    ok &= Check(mesh.vertices.size() >= 3, "triangle has vertices");
    ok &= Check(ContainsApprox(mesh, 1.0f, 0.0f, 0.0f), "+X vertex preserved");
    ok &= Check(ContainsApprox(mesh, 0.0f, 0.0f, 1.0f), "+Y vertex rotated onto +Z");
    ok &= Check(ContainsApprox(mesh, 0.0f, 0.0f, 0.0f), "origin preserved");
    return ok;
}

bool TestCapsuleStandsUp(const fs::path& repoRoot) {
    MeshData mesh;
    std::string error;
    const fs::path path = repoRoot / ".engine" / "meshes" / "Capsule.glb";
    if (!Check(ModelImporter::LoadGLTF(path, mesh, error), "load Capsule.glb: " + error)) {
        return false;
    }
    const float ex = ExtentX(mesh.bounds);
    const float ey = ExtentY(mesh.bounds);
    const float ez = ExtentZ(mesh.bounds);
    return Check(ey > ex && ey > ez,
                 "capsule long axis is Y after node bake, extents xyz=" +
                     std::to_string(ex) + "," + std::to_string(ey) + "," + std::to_string(ez));
}

bool TestCameraUpright(const fs::path& repoRoot) {
    MeshData mesh;
    std::string error;
    const fs::path path = repoRoot / ".engine" / "meshes" / "Camera.glb";
    if (!Check(ModelImporter::LoadGLTF(path, mesh, error), "load Camera.glb: " + error)) {
        return false;
    }
    const float ex = ExtentX(mesh.bounds);
    const float ey = ExtentY(mesh.bounds);
    const float ez = ExtentZ(mesh.bounds);
    bool ok = true;
    ok &= Check(ey > ez,
                "camera stands up (Y taller than Z), extents xyz=" +
                    std::to_string(ex) + "," + std::to_string(ey) + "," + std::to_string(ez));
    ok &= Check(ex > ey,
                "camera lens along X (identity camera forward)");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ModelImporterTests <repo_root>\n";
        return 2;
    }

    const fs::path root = fs::path(argv[1]);
    const fs::path outDir = root / "build" / "model_importer_test_out";
    fs::create_directories(outDir);
    fs::remove_all(outDir / "scratch");
    const fs::path scratch = outDir / "scratch";
    fs::create_directories(scratch);

    bool ok = true;
    ok &= TestNodeRotationBake(scratch);
    ok &= TestCapsuleStandsUp(root);
    ok &= TestCameraUpright(root);

    if (!ok) {
        return 1;
    }

    std::cout << "ModelImporter tests passed.\n";
    return 0;
}
