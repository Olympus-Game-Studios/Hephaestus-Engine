/**
 * @file AssetThumbnailRendererTests.cpp
 * @brief Headless checks for Content Browser preview renders.
 */

#include "Assets/AssetThumbnailRenderer.h"
#include "Gfx/BuiltinMeshes.h"
#include "Scene/PrefabSerializer.h"
#include "Scene/PrefabComponentIO.h"
#include "UI/MaterialEditor/MaterialAssetSerializer.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace Hephaestus;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

const uint8_t* Pixel(const ThumbnailImage& image, int x, int y) {
    return &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
}

bool TestMaterialSphere() {
    MeshData sphere;
    BuildBuiltinMesh("builtin://sphere", sphere);
    ThumbnailMaterial red;
    red.params.albedo = Vec3(0.8f, 0.05f, 0.05f);
    const ThumbnailImage image = RenderThumbnail({ { &sphere, Mat4::Identity(), { &red } } }, 64, ThumbnailView::Front);
    bool ok = Check(image.width == 64 && image.height == 64, "sphere image is 64x64");
    const uint8_t* center = Pixel(image, 32, 32);
    ok &= Check(center[3] == 255, "sphere covers the centre");
    ok &= Check(center[0] > center[1] + 40 && center[0] > center[2] + 40, "red material shades red");
    ok &= Check(Pixel(image, 0, 0)[3] == 0 && Pixel(image, 63, 63)[3] == 0, "corners stay transparent");
    return ok;
}

bool TestFramingAndScale() {
    MeshData cube;
    BuildBuiltinMesh("builtin://cube", cube);
    const Mat4 stretched = Mat4::Multiply(Mat4::Translation(Vec3(40.0f, -3.0f, 7.0f)), Mat4::Scale(Vec3(10.0f, 0.5f, 3.0f)));
    const ThumbnailImage image = RenderThumbnail({ { &cube, stretched, {} } }, 48, ThumbnailView::ThreeQuarter);
    bool ok = Check(Pixel(image, 24, 24)[3] == 255, "off-origin, scaled mesh is framed in the centre");
    int covered = 0;
    for (int i = 3; i < static_cast<int>(image.rgba.size()); i += 4) covered += image.rgba[i] > 0;
    ok &= Check(covered > 48 * 48 / 8, "mesh fills a meaningful part of the image");
    return ok;
}

bool TestNothingToDraw() {
    MeshData cube;
    BuildBuiltinMesh("builtin://cube", cube);
    ThumbnailMaterial cutout;
    cutout.params.opacity = 0.2f;
    cutout.params.opacityMask = 0.5f;
    bool ok = Check(RenderThumbnail({}, 32, ThumbnailView::Front).rgba.empty(), "no draws gives an empty image");
    const ThumbnailImage discarded = RenderThumbnail({ { &cube, Mat4::Identity(), { &cutout } } }, 32, ThumbnailView::Front);
    ok &= Check(Pixel(discarded, 16, 16)[3] == 0, "discarded material draws nothing, as in triangle.frag");
    return ok;
}

bool TestShrink() {
    ThumbnailImage image;
    image.width = 1024;
    image.height = 300;
    image.rgba.assign(static_cast<size_t>(image.width) * image.height * 4, 200);
    ShrinkThumbnailImage(image, 256);
    return Check(image.width == 256 && image.height == 75 && image.rgba.size() == 256u * 75u * 4u && image.rgba[0] == 200,
                 "shrink halves to fit and keeps values");
}

bool TestPrefabAsset(const fs::path& dir) {
    MaterialComponent blue;
    blue.albedo = Vec3(0.05f, 0.1f, 0.9f);
    MaterialEditor::Graph graph;
    MaterialEditor::BuildMinimalGraphFromLegacyPbr(blue, graph);
    std::string error;
    const fs::path materialFile = dir / "assets" / "Blue.hematerial";
    fs::create_directories(materialFile.parent_path());
    bool ok = Check(MaterialEditor::SaveMaterialAssetCompat(materialFile, blue, "Blue", graph, {}, error),
                    "write material: " + error);

    PrefabAsset prefab;
    prefab.name = "Crate";
    prefab.rootNodeId = "root";
    PrefabNode root;
    root.id = "root";
    root.name = "Crate";
    PrefabComponentEntry mesh;
    mesh.type = "MeshRenderer";
    mesh.data = PrefabComponentIO::MakeObject();
    mesh.data.objectValue["meshSource"] = PrefabComponentIO::MakeString("builtin://cube");
    JsonValue materials = PrefabComponentIO::MakeArray();
    materials.arrayValue.push_back(PrefabComponentIO::MakeString("assets/Blue.hematerial"));
    mesh.data.objectValue["appliedMaterialPaths"] = materials;
    root.components.push_back(mesh);
    prefab.nodes.push_back(root);
    const fs::path prefabFile = dir / "assets" / "Crate.hpprefab";
    ok &= Check(PrefabSerializer::Serialize(prefab, prefabFile, error), "write prefab: " + error);

    ThumbnailImage image;
    std::vector<fs::path> dependencies;
    ok &= Check(RenderAssetThumbnail(dir, dir / "assets", prefabFile, AssetType::Prefab, 64, image, dependencies, error),
                "render prefab: " + error);
    if (!ok) return false;
    const uint8_t* center = Pixel(image, 32, 32);
    ok &= Check(center[3] == 255 && center[2] > center[0] + 40, "prefab cube uses its applied blue material");
    bool listsMaterial = false;
    for (const fs::path& dependency : dependencies) {
        listsMaterial |= fs::equivalent(dependency, materialFile);
    }
    ok &= Check(listsMaterial, "material file is reported as a dependency");

    ok &= Check(!RenderAssetThumbnail(dir, dir / "assets", dir / "missing.hpprefab", AssetType::Prefab, 64, image, dependencies, error),
                "missing prefab fails");
    return ok;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "hephaestus_thumbnail_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    bool ok = true;
    ok &= TestMaterialSphere();
    ok &= TestFramingAndScale();
    ok &= TestNothingToDraw();
    ok &= TestShrink();
    ok &= TestPrefabAsset(dir);

    fs::remove_all(dir, ec);
    if (!ok) {
        return 1;
    }
    std::cout << "[PASS] AssetThumbnailRendererTests\n";
    return 0;
}
