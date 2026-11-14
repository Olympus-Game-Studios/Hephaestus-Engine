#include "Assets/AssetIntake.h"
#include "Assets/GltfDocument.h"
#include "Assets/ModelImporter.h"
#include "UI/MaterialEditor/MaterialAssetSerializer.h"
#include "UI/MaterialEditor/MaterialGraphEvaluator.h"

#include <nlohmann/json.hpp>
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../extern/glfw/deps/stb_image_write.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
using Hephaestus::AssetIntake;
using Hephaestus::AssetIntakeRequest;
using Hephaestus::AssetIntakeResult;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<uint8_t> Png(int width, int height, int channels, const std::vector<uint8_t>& pixels) {
    int length = 0;
    unsigned char* data = stbi_write_png_to_mem(pixels.data(), width * channels, width, height, channels, &length);
    std::vector<uint8_t> out(data, data + length);
    STBIW_FREE(data);
    return out;
}

std::vector<uint8_t> Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> pixels;
    for (int i = 0; i < 4; ++i) {
        pixels.insert(pixels.end(), {r, g, b, a});
    }
    return Png(2, 2, 4, pixels);
}

// Value of the first pixel of an image in the project, as stb_image loads it with `channels` channels.
std::vector<int> FirstPixel(const fs::path& project, const std::string& portable, int channels) {
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load((project / portable).string().c_str(), &w, &h, &n, channels);
    std::vector<int> out;
    if (pixels) {
        out.assign(pixels, pixels + channels);
        stbi_image_free(pixels);
    }
    return out;
}

void Append(std::vector<uint8_t>& bin, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    bin.insert(bin.end(), bytes, bytes + size);
    while (bin.size() % 4) {
        bin.push_back(0);
    }
}

// Two meshes under a translated parent, three primitives over two materials, one embedded texture.
void WriteTestGlb(const fs::path& path) {
    std::vector<uint8_t> bin;
    json views = json::array();
    json accessors = json::array();
    auto view = [&](const void* data, size_t size) {
        const size_t offset = bin.size();
        Append(bin, data, size);
        views.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", size}});
        return static_cast<int>(views.size() - 1);
    };
    auto accessor = [&](const void* data, size_t size, int componentType, int count, const char* type) {
        accessors.push_back({{"bufferView", view(data, size)}, {"componentType", componentType}, {"count", count},
                             {"type", type}});
        return static_cast<int>(accessors.size() - 1);
    };
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float uvs[] = {0.25f, 0.5f, 1, 0, 0, 1};
    const uint16_t indices[] = {0, 1, 2};
    const int pos = accessor(positions, sizeof(positions), 5126, 3, "VEC3");
    const int nrm = accessor(normals, sizeof(normals), 5126, 3, "VEC3");
    const int uv = accessor(uvs, sizeof(uvs), 5126, 3, "VEC2");
    const int idx = accessor(indices, sizeof(indices), 5123, 3, "SCALAR");
    auto primitive = [&](int material) {
        return json{{"attributes", {{"POSITION", pos}, {"NORMAL", nrm}, {"TEXCOORD_0", uv}}},
                    {"indices", idx}, {"material", material}};
    };
    const std::vector<uint8_t> baseColor = Rgba(10, 20, 30, 255);
    const std::vector<uint8_t> packed = Rgba(0, 200, 50, 255);
    const int baseView = view(baseColor.data(), baseColor.size());
    const int packedView = view(packed.data(), packed.size());
    json root = {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", {{{"nodes", {0}}}}},
        {"nodes", {{{"translation", {0, 5, 0}}, {"mesh", 0}, {"children", {1}}}, {{"mesh", 1}}}},
        {"meshes", {{{"primitives", {primitive(0)}}}, {{"primitives", {primitive(1), primitive(0)}}}}},
        {"materials",
         {{{"name", "Painted Metal"},
           {"pbrMetallicRoughness",
            {{"baseColorTexture", {{"index", 0}}}, {"metallicRoughnessTexture", {{"index", 1}}}}}},
          {{"name", "Glass"}, {"alphaMode", "BLEND"},
           {"pbrMetallicRoughness", {{"baseColorFactor", {0.5, 0.5, 1.0, 0.25}}, {"metallicFactor", 0.0}}}}}},
        {"textures", {{{"source", 0}}, {{"source", 1}}}},
        {"images", {{{"bufferView", baseView}, {"mimeType", "image/png"}},
                    {{"bufferView", packedView}, {"mimeType", "image/png"}}}},
        {"buffers", {{{"byteLength", bin.size()}}}},
        {"bufferViews", views},
        {"accessors", accessors},
    };
    std::string text = root.dump();
    while (text.size() % 4) {
        text.push_back(' ');
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    u32(0x46546C67);
    u32(2);
    u32(static_cast<uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
    u32(static_cast<uint32_t>(text.size()));
    u32(0x4E4F534A);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    u32(static_cast<uint32_t>(bin.size()));
    u32(0x004E4942);
    out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
}

// A ZIP whose odd entries are deflated (raw deflate from stb's zlib stream) and even entries stored.
void WriteZip(const fs::path& path, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) {
    std::vector<uint8_t> local;
    std::vector<uint8_t> central;
    auto put16 = [](std::vector<uint8_t>& v, uint32_t x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); };
    auto put32 = [&](std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); };
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& [name, data] = entries[i];
        std::vector<uint8_t> stored = data;
        uint16_t method = 0;
        if (i % 2 == 1) {
            int length = 0;
            unsigned char* z = stbi_zlib_compress(const_cast<unsigned char*>(data.data()),
                                                  static_cast<int>(data.size()), &length, 8);
            stored.assign(z + 2, z + length - 4);
            STBIW_FREE(z);
            method = 8;
        }
        const uint32_t offset = static_cast<uint32_t>(local.size());
        for (std::vector<uint8_t>* v : {&local, &central}) {
            put32(*v, v == &local ? 0x04034b50 : 0x02014b50);
            if (v == &central) put16(*v, 20);
            put16(*v, 20);
            put16(*v, 0);
            put16(*v, method);
            put32(*v, 0);
            put32(*v, 0); // CRC is not checked by the reader
            put32(*v, static_cast<uint32_t>(stored.size()));
            put32(*v, static_cast<uint32_t>(data.size()));
            put16(*v, static_cast<uint32_t>(name.size()));
            put16(*v, 0);
            if (v == &central) {
                put16(*v, 0);
                put16(*v, 0);
                put16(*v, 0);
                put32(*v, 0);
                put32(*v, offset);
            }
            v->insert(v->end(), name.begin(), name.end());
        }
        local.insert(local.end(), stored.begin(), stored.end());
    }
    std::vector<uint8_t> end;
    put32(end, 0x06054b50);
    put32(end, 0);
    put16(end, static_cast<uint32_t>(entries.size()));
    put16(end, static_cast<uint32_t>(entries.size()));
    put32(end, static_cast<uint32_t>(central.size()));
    put32(end, static_cast<uint32_t>(local.size()));
    put16(end, 0);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto* part : {&local, &central, &end}) {
        out.write(reinterpret_cast<const char*>(part->data()), static_cast<std::streamsize>(part->size()));
    }
}

bool TestStaticGltfLoad(const fs::path& scratch) {
    bool ok = true;
    const fs::path glb = scratch / "Props.glb";
    WriteTestGlb(glb);
    MeshData mesh;
    std::vector<int> slots;
    std::string error;
    ok &= Check(ModelImporter::LoadGLTF(glb, mesh, error, &slots), "glTF loads: " + error);
    ok &= Check(mesh.vertices.size() == 6, "every mesh in the scene is imported, not only meshes[0], and primitives sharing vertices load them once");
    ok &= Check(mesh.submeshes.size() == 2 && slots == std::vector<int>({0, 1}),
                "one submesh per glTF material, slot order by first use");
    ok &= Check(mesh.submeshes.size() == 2 && mesh.submeshes[0].indexCount == 6 &&
                    mesh.submeshes[0].name == "Painted Metal" && mesh.submeshes[1].name == "Glass",
                "primitives sharing a material are merged into its submesh");
    ok &= Check(!mesh.vertices.empty() && std::abs(mesh.vertices[0].pos[1] - 5.0f) < 1e-5f &&
                    std::abs(mesh.vertices[0].texCoord[0] - 0.25f) < 1e-6f &&
                    std::abs(mesh.vertices[0].texCoord[1] - 0.5f) < 1e-6f,
                "node transforms are baked and UVs are kept");

    // Split FBX meshes are written as GLB; reading one back must give the same geometry and slots.
    const fs::path written = scratch / "Written.glb";
    ok &= Check(Hephaestus::Gltf::WriteGlb(written, mesh, {"Painted Metal", "Glass"}, error), "GLB writes: " + error);
    MeshData reread;
    std::vector<int> rereadSlots;
    ok &= Check(ModelImporter::LoadGLTF(written, reread, error, &rereadSlots) &&
                    reread.vertices.size() == mesh.vertices.size() && reread.indices == mesh.indices &&
                    reread.submeshes.size() == 2 && reread.submeshes[1].name == "Glass" &&
                    std::abs(reread.vertices[0].texCoord[0] - 0.25f) < 1e-6f,
                "a written GLB reads back with the same geometry, UVs and material slots");
    return ok;
}

bool TestImportModel(const fs::path& scratch) {
    bool ok = true;
    const fs::path project = scratch / "ModelProject";
    fs::create_directories(project / "assets");
    const fs::path pack = scratch / "Download";
    fs::create_directories(pack);
    WriteTestGlb(pack / "Props.glb");

    AssetIntakeRequest request;
    request.source = pack / "Props.glb";
    request.projectRoot = project;
    request.targetFolder = "assets/Imported/Sketchfab/Props";
    request.credit = {"Props", "Sketchfab", "https://sketchfab.com/3d-models/props", "Some Artist", "",
                      "CC Attribution", "http://creativecommons.org/licenses/by/4.0/"};
    AssetIntakeResult result;
    std::string error;
    ok &= Check(AssetIntake::Import(request, result, error), "model import succeeds: " + error);
    ok &= Check(result.prefabs.size() == 1 && result.materials.size() == 2, "one prefab and two materials");
    if (result.prefabs.size() != 1 || result.materials.size() != 2) {
        return false;
    }
    const json prefab = json::parse(ReadText(project / result.prefabs[0]));
    const json mesh = prefab["nodes"][1]["components"][0]["data"];
    ok &= Check(mesh["meshSource"] == "assets/Imported/Sketchfab/Props/Models/Props.glb", "prefab uses the copied model");
    ok &= Check(mesh["appliedMaterialPaths"] == json(result.materials) && mesh["submeshCount"] == 2,
                "prefab applies a material to every slot");
    const json settings =
        json::parse(ReadText(project / "assets/Imported/Sketchfab/Props/Models/Props.glb.hpmesh"));
    ok &= Check(settings["appliedMaterialPaths"] == json(result.materials),
                "the model itself keeps its materials as slot defaults");

    const json metal = json::parse(ReadText(project / result.materials[0]));
    ok &= Check(metal["useAlbedoTexture"] == true && FirstPixel(project, metal["albedoTexturePath"], 3) ==
                                                         std::vector<int>({10, 20, 30}),
                "embedded base colour texture is written");
    ok &= Check(metal["roughnessTexturePath"] == metal["metallicTexturePath"] &&
                    FirstPixel(project, metal["roughnessTexturePath"], 4) == std::vector<int>({0, 200, 50, 255}),
                "the packed metal/rough map is kept as one texture");
    ok &= Check(metal["roughnessChannel"] == 1 && metal["metallicChannel"] == 2 && !metal.contains("aoChannel"),
                "metallic comes from the blue channel of the packed map");
    ok &= Check(metal["roughness"] == 1.0 && metal["metallic"] == 1.0, "glTF factors default to 1");
    const json glass = json::parse(ReadText(project / result.materials[1]));
    ok &= Check(glass["blendMode"] == "alphaBlend" && std::abs(glass["opacity"].get<double>() - 0.25) < 1e-6,
                "blended glTF material keeps its opacity");

    const json credit = json::parse(ReadText(project / "assets/Imported/Sketchfab/Props/credits.json"));
    ok &= Check(credit["author"] == "Some Artist", "credits.json records the author");
    const std::string lua = ReadText(project / "assets/scripts/ImportedCredits.lua");
    ok &= Check(lua.find("role = \"\\\"PROPS\\\"\"") != std::string::npos &&
                    lua.find("names = { \"Some Artist\" }") != std::string::npos &&
                    lua.find("Licensed under CC Attribution") != std::string::npos,
                "generated credits module lists the asset:\n" + lua);

    AssetIntakeResult again;
    ok &= Check(AssetIntake::Import(request, again, error) && again.folder == "assets/Imported/Sketchfab/Props_2",
                "a second import goes to a new folder instead of overwriting");
    return ok;
}

bool TestImportZippedTextureSet(const fs::path& scratch) {
    bool ok = true;
    const fs::path project = scratch / "ZipProject";
    fs::create_directories(project / "assets");
    const std::string readme = "Megascans surface";
    const fs::path zip = scratch / "Rock.zip";
    WriteZip(zip, {
        {"Rock/T_Rock_4K_Albedo.png", Rgba(90, 80, 70, 255)},
        {"Rock/T_Rock_4K_normdx.png", Rgba(128, 100, 255, 255)},
        {"Rock/T_Rock_4K_ORM.png", Rgba(30, 150, 0, 255)},
        {"Rock/Rock_Displacement.exr", std::vector<uint8_t>(16, 7)},
        {"Rock/readme.txt", std::vector<uint8_t>(readme.begin(), readme.end())},
    });
    AssetIntakeRequest request;
    request.source = zip;
    request.projectRoot = project;
    request.targetFolder = "assets/Imported/Fab/Rock";
    AssetIntakeResult result;
    std::string error;
    ok &= Check(AssetIntake::Import(request, result, error), "zipped texture set imports: " + error);
    ok &= Check(result.prefabs.empty() && result.materials.size() == 1, "a texture-only pack becomes a material");
    if (result.materials.size() != 1) {
        return false;
    }
    const json rock = json::parse(ReadText(project / result.materials[0]));
    ok &= Check(FirstPixel(project, rock["albedoTexturePath"], 3) == std::vector<int>({90, 80, 70}),
                "albedo matched by Megascans suffix (stored entry)");
    ok &= Check(FirstPixel(project, rock["normalTexturePath"], 3) == std::vector<int>({128, 155, 255}),
                "DirectX normal map has its green channel flipped (deflated entry)");
    ok &= Check(rock["aoTexturePath"] == rock["roughnessTexturePath"] &&
                    rock["roughnessTexturePath"] == rock["metallicTexturePath"] &&
                    FirstPixel(project, rock["aoTexturePath"], 4) == std::vector<int>({30, 150, 0, 255}) &&
                    !rock.contains("aoChannel") && rock["roughnessChannel"] == 1 && rock["metallicChannel"] == 2,
                "one ORM map feeds AO, roughness and metallic from its R, G and B channels");

    MaterialComponent loaded;
    std::string displayName;
    MaterialEditor::Graph graph;
    ok &= Check(MaterialEditor::LoadMaterialAssetCompat(project / result.materials[0], loaded, displayName, graph,
                                                        error) &&
                    loaded.roughnessChannel == 1 && loaded.metallicChannel == 2 && loaded.aoChannel == 0,
                "material files load their map channels");
    MaterialEditor::Graph rebuilt;
    MaterialEditor::BuildMinimalGraphFromLegacyPbr(loaded, rebuilt);
    MaterialComponent evaluated;
    ok &= Check(MaterialEditor::EvaluateGraph(rebuilt, evaluated, error) && evaluated.roughnessChannel == 1 &&
                    evaluated.metallicChannel == 2 && evaluated.aoChannel == 0 &&
                    evaluated.roughnessTexturePath == loaded.roughnessTexturePath,
                "opening the material in the graph editor keeps its channels (Texture Sample > Break Vector)");
    ok &= Check(ReadText(project / "assets/Imported/Fab/Rock/readme.txt") == readme,
                "documents in the pack are kept");
    ok &= Check(fs::exists(project / "assets/Imported/Fab/Rock/Textures/Rock_Displacement.exr"),
                "maps the renderer cannot use are still copied");
    ok &= Check(!fs::exists(project / ".hephaestus" / "intake") || fs::is_empty(project / ".hephaestus" / "intake"),
                "the extraction folder is removed");
    ok &= Check(!fs::exists(project / "assets/scripts/ImportedCredits.lua"), "no credits without a credit");
    return ok;
}

// Laid out like a Fab listing with additional files: the export's model, the same model again in a seller folder
// in two formats, plain PBR maps, and engine-specific copies (UE_Textures) whose normal map must not win.
bool TestDuplicateModelsAndEngineTextures(const fs::path& scratch) {
    const fs::path project = scratch / "ListingProject";
    fs::create_directories(project / "assets");
    const fs::path listing = scratch / "Listing";
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/1 2/1 3/1\n";
    fs::create_directories(listing / "source");
    fs::create_directories(listing / "seller" / "Model");
    fs::create_directories(listing / "seller" / "Textures" / "GasGrenade" / "UE_Textures");
    std::ofstream(listing / "source" / "Gas_Grenade.obj") << obj;
    std::ofstream(listing / "seller" / "Model" / "Gas_Grenade.obj") << obj;
    const fs::path textures = listing / "seller" / "Textures" / "GasGrenade";
    auto writePng = [](const fs::path& path, const std::vector<uint8_t>& png) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    };
    writePng(textures / "GasGrenade_BaseColor.png", Rgba(40, 60, 20, 255));
    writePng(textures / "GasGrenade_Normal.png", Rgba(128, 128, 255, 255));
    writePng(textures / "UE_Textures" / "GasGrenade_Normal.png", Rgba(128, 10, 255, 255));
    writePng(textures / "UE_Textures" / "GasGrenade_OcclusionRoughnessMetallic.png", Rgba(200, 90, 0, 255));

    AssetIntakeRequest request;
    request.source = listing;
    request.projectRoot = project;
    request.targetFolder = "assets/Imported/Fab/Grenades";
    AssetIntakeResult result;
    std::string error;
    bool ok = Check(AssetIntake::Import(request, result, error), "listing folder imports: " + error);
    ok &= Check(result.prefabs.size() == 1, "one prefab per model name, not one per copy");
    ok &= Check(std::any_of(result.warnings.begin(), result.warnings.end(),
                            [](const std::string& w) { return w.find("copies of models") != std::string::npos; }),
                "skipped copies are reported");
    if (result.materials.empty()) {
        return false;
    }
    const json material = json::parse(ReadText(project / result.materials[0]));
    ok &= Check(FirstPixel(project, material.value("albedoTexturePath", ""), 3) == std::vector<int>({40, 60, 20}),
                "the model named Gas_Grenade finds the GasGrenade texture set");
    ok &= Check(FirstPixel(project, material.value("normalTexturePath", ""), 3) == std::vector<int>({128, 128, 255}),
                "the plain normal map wins over the UE_Textures copy");
    ok &= Check(material.value("aoTexturePath", "") != "" && material.value("roughnessChannel", 0) == 1,
                "an OcclusionRoughnessMetallic map fills the maps the set lacks");
    return ok;
}

bool TestUntexturedPackWarns(const fs::path& scratch) {
    const fs::path project = scratch / "UntexturedProject";
    fs::create_directories(project / "assets");
    const fs::path pack = scratch / "Untextured";
    fs::create_directories(pack);
    std::ofstream(pack / "Crate.obj") << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    AssetIntakeRequest request;
    request.source = pack;
    request.projectRoot = project;
    request.targetFolder = "assets/Imported/Fab/Crate";
    AssetIntakeResult result;
    std::string error;
    bool ok = Check(AssetIntake::Import(request, result, error) && result.prefabs.size() == 1,
                    "a pack without textures still imports: " + error);
    ok &= Check(!result.warnings.empty() && result.warnings.back().find("no textures") != std::string::npos,
                "a pack without textures says so");
    return ok;
}

bool TestRejections(const fs::path& scratch) {
    bool ok = true;
    const fs::path project = scratch / "RejectProject";
    fs::create_directories(project / "assets");
    AssetIntakeResult result;
    std::string error;

    const fs::path evil = scratch / "evil.zip";
    WriteZip(evil, {{"../../outside.txt", {'x'}}});
    ok &= Check(!AssetIntake::ExtractZip(evil, scratch / "evil_out", error) &&
                    error.find("unsafe path") != std::string::npos && !fs::exists(scratch / "outside.txt"),
                "zip entries cannot escape the extraction folder");

    const fs::path unreal = scratch / "UnrealOnly";
    fs::create_directories(unreal);
    std::ofstream(unreal / "SM_Crate.uasset") << "x";
    AssetIntakeRequest request;
    request.source = unreal;
    request.projectRoot = project;
    request.targetFolder = "assets/Imported/Fab/Crate";
    ok &= Check(!AssetIntake::Import(request, result, error) && error.find("Unreal") != std::string::npos,
                "an Unreal-only pack is refused with an explanation");

    request.source = scratch / "Download" / "Props.glb";
    request.targetFolder = "assets/../outside";
    ok &= Check(!AssetIntake::Import(request, result, error), "targets outside assets/ are refused");
    request.targetFolder = "assets";
    ok &= Check(!AssetIntake::Import(request, result, error), "the assets folder itself is not a target");
    return ok;
}

} // namespace

// A kit laid out like KitBash3D Cargo's: the asset's root layer pulls geometry and material bindings in through a
// payload, materials come from their own files through "../" paths and an internal reference, and the kit keeps
// one Textures/<variant> folder whose maps are only named after the materials.
void WriteUsdKit(const fs::path& kit) {
    fs::create_directories(kit / "Models/Box");
    fs::create_directories(kit / "Materials/Paint");
    fs::create_directories(kit / "Textures/png2k");
    std::ofstream(kit / "Models/Box/Box.usd") << R"(#usda 1.0
(
    defaultPrim = "Box"
    metersPerUnit = 0.01
    upAxis = "Z"
)

def Xform "Box" (
    prepend payload = @./payload.usd@
)
{
}
)";
    std::ofstream(kit / "Models/Box/payload.usd") << R"(#usda 1.0
(
    defaultPrim = "Box"
)

def Xform "Box" (
    prepend references = [@./mtl.usd@, @./geo.usd@]
)
{
}
)";
    std::ofstream(kit / "Models/Box/mtl.usd") << R"(#usda 1.0
(
    defaultPrim = "Box"
)

over "Box"
{
    def Scope "mtl"
    {
        def Material "Paint" (
            prepend references = @../../Materials/Paint/Paint.usd@</Paint>
        )
        {
        }
    }
}
)";
    std::ofstream(kit / "Models/Box/geo.usd") << R"(#usda 1.0
(
    defaultPrim = "Box"
)

over "Box"
{
    def Scope "geo"
    {
        def Mesh "Quad" (
            prepend apiSchemas = ["MaterialBindingAPI"]
        )
        {
            int[] faceVertexCounts = [4]
            int[] faceVertexIndices = [0, 1, 2, 3]
            point3f[] points = [(0, 0, 0), (100, 0, 0), (100, 0, 200), (0, 0, 200)]
            normal3f[] normals = [(0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0)] (
                interpolation = "vertex"
            )
            texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (
                interpolation = "vertex"
            )
            rel material:binding = </Box/mtl/Paint>
        }

        def Mesh "UCX_Quad"
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(0, 0, 0), (100, 0, 0), (100, 0, 200)]
        }

        def Xform "WheelA" (
            instanceable = true
            prepend references = </Box/__prototypes__/Wheel>
        )
        {
            double3 xformOp:translate = (300, 0, 0)
            uniform token[] xformOpOrder = ["xformOp:translate"]
        }

        def Xform "WheelB" (
            instanceable = true
            prepend references = </Box/__prototypes__/Wheel>
        )
        {
            double3 xformOp:translate = (-300, 0, 0)
            uniform token[] xformOpOrder = ["xformOp:translate"]
        }
    }

    class "__prototypes__"
    {
        class Xform "Wheel"
        {
            def Mesh "Tyre" (
                prepend apiSchemas = ["MaterialBindingAPI"]
            )
            {
                rel material:binding = </Box/mtl/Paint>
                int[] faceVertexCounts = [3, 3]
                int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
                point3f[] points = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
            }
        }
    }
}
)";
    std::ofstream(kit / "Materials/Paint/Paint.usd") << R"(#usda 1.0
(
    defaultPrim = "Paint"
)

def Material "Paint" (
    prepend references = </__src__/Paint>
)
{
}

class "__src__"
{
    def Material "Paint"
    {
        token outputs:surface.connect = </__src__/Paint/Surface.outputs:surface>

        def Shader "Surface"
        {
            uniform token info:id = "UsdPreviewSurface"
            float inputs:roughness = 0.25
            float inputs:metallic = 0.75
            token outputs:surface
        }
    }
}
)";
    const std::vector<uint8_t> red = Rgba(200, 10, 10, 255);
    std::ofstream(kit / "Textures/png2k/Paint_basecolor.png", std::ios::binary)
        .write(reinterpret_cast<const char*>(red.data()), static_cast<std::streamsize>(red.size()));
    const std::vector<uint8_t> blue = Rgba(10, 10, 200, 255);
    std::ofstream(kit / "Textures/png2k/Other_basecolor.png", std::ios::binary)
        .write(reinterpret_cast<const char*>(blue.data()), static_cast<std::streamsize>(blue.size()));
}

bool TestImportUsdKit(const fs::path& scratch) {
    bool ok = true;
    const fs::path project = scratch / "UsdProject";
    fs::create_directories(project / "assets");
    const fs::path kit = scratch / "UsdKit";
    WriteUsdKit(kit);
    AssetIntakeRequest request;
    request.source = kit / "Models/Box/Box.usd";
    request.projectRoot = project;
    request.targetFolder = "assets/Imported/Cargo/Box";
    AssetIntakeResult result;
    std::string error;
    ok &= Check(AssetIntake::Import(request, result, error), "USD kit imports: " + error);
    ok &= Check(result.models == std::vector<std::string>({"assets/Imported/Cargo/Box/Models/Box/Quad.glb",
                                                          "assets/Imported/Cargo/Box/Models/Box/UCX_Quad.glb",
                                                          "assets/Imported/Cargo/Box/Models/Box/Tyre.glb"}) &&
                    result.prefabs.size() == 1 && result.materials.size() == 1,
                "each Mesh prim becomes its own model; the two wheel instances share one");
    if (!ok) {
        return false;
    }
    MeshData mesh;
    ok &= Check(ModelImporter::LoadGLTF(project / result.models[0], mesh, error), "converted GLB loads: " + error);
    ok &= Check(mesh.indices.size() == 6 && mesh.submeshes.size() == 1 && mesh.submeshes[0].name == "Paint",
                "the quad is triangulated into one slot named after its material");
    ok &= Check(std::abs(mesh.bounds.max.x - 1.0f) < 1e-4f && std::abs(mesh.bounds.max.y - 2.0f) < 1e-4f &&
                    std::abs(mesh.bounds.min.z) < 1e-4f,
                "centimetres and Z up become metres and Y up");
    const json paint = json::parse(ReadText(project / result.materials[0]));
    ok &= Check(std::abs(paint["roughness"].get<double>() - 0.25) < 1e-6 &&
                    std::abs(paint["metallic"].get<double>() - 0.75) < 1e-6,
                "the shader behind the material file's internal reference is read");
    ok &= Check(paint["useAlbedoTexture"] == true &&
                    FirstPixel(project, paint["albedoTexturePath"], 3) == std::vector<int>({200, 10, 10}),
                "the kit's Textures folder supplies the map named after the material");
    const json prefab = json::parse(ReadText(project / result.prefabs[0]));
    std::vector<std::string> tyres;
    bool collisionHidden = false;
    for (const json& node : prefab["nodes"]) {
        if (node.value("name", "") == "UCX_Quad") {
            bool hasCollider = false;
            for (const json& component : node["components"]) {
                hasCollider |= component["type"] == "MeshCollider" && component["data"]["useComplexCollision"] == true &&
                               component["data"]["isStatic"] == true;
                if (component["type"] == "MeshRenderer") {
                    collisionHidden = component["data"]["visible"] == false &&
                                      component["data"]["appliedMaterialPaths"].empty();
                }
            }
            collisionHidden &= hasCollider;
        }
        if (node.value("name", "") == "WheelA" || node.value("name", "") == "WheelB") {
            const float x = node["transform"]["position"][0].get<float>();
            ok &= Check(std::abs(std::abs(x) - 3.0f) < 1e-4f, "each wheel group sits where its instance is");
        }
        for (const json& component : node.value("components", json::array())) {
            const std::string source = component["data"].value("meshSource", "");
            if (source.find("Tyre") != std::string::npos) {
                tyres.push_back(node.value("parentId", ""));
            }
        }
    }
    ok &= Check(collisionHidden, "a UCX_ collision mesh becomes a hidden static mesh collider without materials");
    ok &= Check(tyres.size() == 2 && tyres[0] != tyres[1] && tyres[0] != "node_0",
                "the prototype in the class prim is only drawn through its two instances, each in its group");
    ok &= Check(!fs::exists(project / "assets/Imported/Cargo/Box/Textures") &&
                    !fs::exists(project / "assets/Imported/Cargo/Box/Materials/Other.hematerial"),
                "the kit's other maps are neither copied nor made into materials");
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: AssetIntakeTests <repo_root>\n";
        return 2;
    }
    const fs::path scratch = fs::path(argv[1]) / "build" / "asset_intake_test_out";
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    bool ok = true;
    ok &= TestStaticGltfLoad(scratch);
    ok &= TestImportModel(scratch);
    ok &= TestImportZippedTextureSet(scratch);
    ok &= TestDuplicateModelsAndEngineTextures(scratch);
    ok &= TestUntexturedPackWarns(scratch);
    ok &= TestRejections(scratch);
    ok &= TestImportUsdKit(scratch);
    std::cout << (ok ? "AssetIntakeTests passed\n" : "AssetIntakeTests FAILED\n");
    return ok ? 0 : 1;
}
