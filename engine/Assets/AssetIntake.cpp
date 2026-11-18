/**
 * @file AssetIntake.cpp
 * @brief Imports a downloaded asset pack into a project with its materials applied through prefabs.
 */

#include "AssetIntake.h"

#include "AssetPathPolicy.h"
#include "FbxDocument.h"
#include "FbxStaticImporter.h"
#include "GltfDocument.h"
#include "MeshSettings.h"
#include "ModelImporter.h"
#include "ModelMaterialImport.h"
#include "UsdModel.h"
#include "../Scene/PrefabComponentIO.h"
#include "../Scene/PrefabOverrideSystem.h"
#include "../Scene/PrefabSerializer.h"

#include <nlohmann/json.hpp>
#include "stb_image.h"

#include <algorithm>
#include <iterator>
#include <cctype>
#include <chrono>
#include <climits>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <tuple>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Hephaestus {
namespace {

constexpr const char* kCreditFile = "credits.json";
constexpr const char* kImportFile = "import.json";
constexpr const char* kImportSchema = "HephaestusAssetImport";
constexpr const char* kCreditSchema = "HephaestusAssetCredit";

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string Ext(const fs::path& path) { return Lower(path.extension().string()); }

bool IsModel(const fs::path& p) {
    const std::string e = Ext(p);
    return e == ".fbx" || e == ".glb" || e == ".gltf" || e == ".obj" || Usd::IsUsd(p);
}
// The formats stb_image, and so the renderer, can load.
bool IsImage(const fs::path& p) {
    const std::string e = Ext(p);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".psd" ||
           e == ".gif" || e == ".hdr";
}
bool IsUnsupportedImage(const fs::path& p) {
    const std::string e = Ext(p);
    return e == ".tif" || e == ".tiff" || e == ".exr" || e == ".dds" || e == ".ktx2" || e == ".webp";
}
bool IsUnreal(const fs::path& p) {
    const std::string e = Ext(p);
    return e == ".uasset" || e == ".umap" || e == ".uexp" || e == ".ubulk";
}
bool IsDocument(const fs::path& p) {
    const std::string e = Ext(p);
    return e == ".txt" || e == ".md" || e == ".pdf" || e == ".html" || e == ".htm" || e == ".rtf";
}

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool ReadAt(std::ifstream& in, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
    out.resize(size);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount()) == size;
}

/// Tokens of a file or material name: lower case, split on separators and camel case is left alone.
std::vector<std::string> Tokens(const std::string& name) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : name) {
        if (c == '_' || c == '-' || c == ' ' || c == '.') {
            if (!current.empty()) {
                tokens.push_back(Lower(current));
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(Lower(current));
    }
    return tokens;
}

bool IsResolutionToken(const std::string& t) {
    if (t.size() >= 2 && t.back() == 'k' &&
        std::all_of(t.begin(), t.end() - 1, [](unsigned char c) { return std::isdigit(c); })) {
        return true;
    }
    return t == "512" || t == "1024" || t == "2048" || t == "4096" || t == "8192" || t == "lod0";
}

/// Name with asset prefixes (T_, M_, MI_, MAT_, SM_) and resolution tokens removed, for matching.
std::string MatchKey(const std::string& name) {
    std::vector<std::string> tokens = Tokens(name);
    while (!tokens.empty() && (tokens.front() == "t" || tokens.front() == "m" || tokens.front() == "mi" ||
                               tokens.front() == "mat" || tokens.front() == "sm")) {
        tokens.erase(tokens.begin());
    }
    std::erase_if(tokens, IsResolutionToken);
    std::string key;
    for (const std::string& t : tokens) {
        key += (key.empty() ? "" : "_") + t;
    }
    return key;
}

enum class MapKind { None, BaseColor, Normal, Roughness, Gloss, Metallic, AO, ORM, MetallicRoughness };

struct TextureSet {
    std::string displayName;
    SourceTexture baseColor, normal, roughness, metallic, ao;
    std::vector<fs::path> files;
    bool used = false;
};

/// Classifies a texture file name by its map suffix (Megascans, Unreal, Substance and glTF exporters).
MapKind Classify(const std::string& stem, std::string& outBase, bool& outDirectX) {
    std::vector<std::string> tokens = Tokens(stem);
    std::erase_if(tokens, IsResolutionToken);
    outDirectX = false;
    if (tokens.size() >= 2 && (tokens.back() == "dx" || tokens.back() == "directx" || tokens.back() == "gl" ||
                               tokens.back() == "opengl")) {
        outDirectX = tokens.back() == "dx" || tokens.back() == "directx";
        tokens.pop_back();
    }
    if (tokens.size() < 2) {
        return MapKind::None;
    }
    std::string last = tokens.back();
    if (last == "normaldx" || last == "normalgl" || last == "normdx" || last == "normgl") {
        outDirectX = last == "normaldx" || last == "normdx";
        last = "normal";
    }
    static const std::map<std::string, MapKind> kinds = {
        {"basecolor", MapKind::BaseColor}, {"basecolour", MapKind::BaseColor}, {"albedo", MapKind::BaseColor},
        {"diffuse", MapKind::BaseColor},   {"diff", MapKind::BaseColor},       {"color", MapKind::BaseColor},
        {"colour", MapKind::BaseColor},    {"col", MapKind::BaseColor},        {"bc", MapKind::BaseColor},
        {"d", MapKind::BaseColor},         {"b", MapKind::BaseColor},          {"base", MapKind::BaseColor},
        {"normal", MapKind::Normal},       {"nrm", MapKind::Normal},           {"nor", MapKind::Normal},
        {"norm", MapKind::Normal},         {"n", MapKind::Normal},             {"roughness", MapKind::Roughness},
        {"rough", MapKind::Roughness},     {"rgh", MapKind::Roughness},        {"r", MapKind::Roughness},
        {"gloss", MapKind::Gloss},         {"glossiness", MapKind::Gloss},     {"metallic", MapKind::Metallic},
        {"metalness", MapKind::Metallic},  {"metal", MapKind::Metallic},       {"m", MapKind::Metallic},
        {"ao", MapKind::AO},               {"occlusion", MapKind::AO},         {"ambientocclusion", MapKind::AO},
        {"occ", MapKind::AO},              {"orm", MapKind::ORM}, {"occlusionroughnessmetallic", MapKind::ORM},              {"arm", MapKind::ORM},
        {"metallicroughness", MapKind::MetallicRoughness},
    };
    const auto found = kinds.find(last);
    if (found == kinds.end()) {
        return MapKind::None;
    }
    tokens.pop_back();
    if (tokens.size() >= 2 && tokens.back() == "ambient" && found->second == MapKind::AO) {
        tokens.pop_back();
    }
    std::string base;
    for (const std::string& t : tokens) {
        base += (base.empty() ? "" : "_") + t;
    }
    outBase = MatchKey(base);
    return outBase.empty() ? MapKind::None : found->second;
}

SourceTexture FileTexture(const fs::path& file, int channel = -1) {
    SourceTexture texture;
    texture.file = file;
    texture.channel = channel;
    return texture;
}

// The first map of each kind wins; images arrive shallowest folder first, so a pack's plain maps beat the
// engine-specific copies some sellers nest beside them (UE_Textures with DirectX normals, for example).
void AddToSet(TextureSet& set, MapKind kind, const fs::path& file, bool directX) {
    set.files.push_back(file);
    switch (kind) {
    case MapKind::BaseColor:
        if (!set.baseColor.Exists()) set.baseColor = FileTexture(file);
        break;
    case MapKind::Normal:
        if (!set.normal.Exists()) {
            set.normal = FileTexture(file);
            set.normal.flipGreen = directX;
        }
        break;
    case MapKind::Roughness:
        if (!set.roughness.Exists()) set.roughness = FileTexture(file);
        break;
    case MapKind::Gloss:
        if (!set.roughness.Exists()) {
            set.roughness = FileTexture(file);
            set.roughness.invert = true;
        }
        break;
    case MapKind::Metallic:
        if (!set.metallic.Exists()) set.metallic = FileTexture(file);
        break;
    case MapKind::AO:
        if (!set.ao.Exists()) set.ao = FileTexture(file);
        break;
    case MapKind::ORM:
        if (!set.ao.Exists()) set.ao = FileTexture(file, 0);
        if (!set.roughness.Exists()) set.roughness = FileTexture(file, 1);
        if (!set.metallic.Exists()) set.metallic = FileTexture(file, 2);
        break;
    case MapKind::MetallicRoughness:
        if (!set.roughness.Exists()) set.roughness = FileTexture(file, 1);
        if (!set.metallic.Exists()) set.metallic = FileTexture(file, 2);
        break;
    case MapKind::None: break;
    }
}

/// Fills only the maps the material does not already reference.
void FillFromSet(SourceMaterial& material, TextureSet& set) {
    set.used = true;
    if (!material.baseColorTexture.Exists() && set.baseColor.Exists()) {
        material.baseColorTexture = set.baseColor;
        material.baseColor = Vec3(1.0f, 1.0f, 1.0f);
    }
    if (!material.normalTexture.Exists()) material.normalTexture = set.normal;
    if (!material.roughnessTexture.Exists() && set.roughness.Exists()) {
        material.roughnessTexture = set.roughness;
        material.roughness = 1.0f;
    }
    if (!material.metallicTexture.Exists() && set.metallic.Exists()) {
        material.metallicTexture = set.metallic;
        material.metallic = 1.0f;
    }
    if (!material.aoTexture.Exists()) material.aoTexture = set.ao;
}

// Keys compare without separators, so a model "Gas_Grenade" finds the "GasGrenade_BaseColor" set.
std::string Squash(std::string key) {
    std::erase(key, '_');
    return key;
}

TextureSet* FindSet(std::map<std::string, TextureSet>& sets, const std::string& name) {
    const std::string key = Squash(MatchKey(name));
    if (key.empty()) {
        return nullptr;
    }
    for (auto& [setKey, set] : sets) {
        if (Squash(setKey) == key) {
            return &set;
        }
    }
    TextureSet* best = nullptr;
    size_t bestLength = 0;
    for (auto& [rawKey, set] : sets) {
        const std::string setKey = Squash(rawKey);
        const bool related = key.find(setKey) != std::string::npos || setKey.find(key) != std::string::npos;
        if (related && setKey.size() > bestLength) {
            best = &set;
            bestLength = setKey.size();
        }
    }
    return best;
}

bool IsUnderFolder(const fs::path& root, const fs::path& candidate) {
    const fs::path rel = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    return !rel.empty() && rel != "." && *rel.begin() != ".." && !rel.is_absolute();
}

bool CopyInto(const fs::path& from, const fs::path& to, std::string& outError) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Could not copy " + from.string() + ": " + ec.message();
        return false;
    }
    return true;
}

/// Copies a model and the files it loads (glTF buffers and images, OBJ material library) beside it.
bool CopyModel(const fs::path& model, const fs::path& folder, fs::path& outCopied, std::vector<fs::path>& outUsed,
               std::string& outError) {
    outCopied = folder / model.filename();
    if (!CopyInto(model, outCopied, outError)) {
        return false;
    }
    std::vector<std::string> dependencies;
    if (Ext(model) == ".gltf") {
        try {
            std::ifstream in(model, std::ios::binary);
            const json root = json::parse(in);
            // Images reach the project through the material writer, so only the geometry buffers are copied.
            for (const json& entry : root.value("buffers", json::array())) {
                const std::string uri = entry.value("uri", "");
                if (!uri.empty() && uri.rfind("data:", 0) != 0) {
                    dependencies.push_back(uri);
                }
            }
        } catch (const std::exception& e) {
            outError = "Invalid glTF " + model.string() + ": " + e.what();
            return false;
        }
    } else if (Ext(model) == ".obj") {
        std::ifstream in(model);
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("mtllib ", 0) == 0) {
                std::string name = line.substr(7);
                while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) {
                    name.pop_back();
                }
                dependencies.push_back(name);
            }
        }
    }
    for (const std::string& dependency : dependencies) {
        const fs::path source = (model.parent_path() / fs::path(dependency)).lexically_normal();
        const fs::path target = (folder / fs::path(dependency)).lexically_normal();
        std::error_code ec;
        if (!IsUnderFolder(model.parent_path(), source) || !IsUnderFolder(folder, target) ||
            !fs::is_regular_file(source, ec)) {
            continue;
        }
        if (!CopyInto(source, target, outError)) {
            return false;
        }
        outUsed.push_back(source);
    }
    return true;
}

std::string LuaString(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        default: out.push_back(c);
        }
    }
    return out + "\"";
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

/// Collision meshes shipped beside the visible ones: Unreal's UCX_/UBX_/USP_/UCP_ prefixes, a "collision" or
/// "collider" word, or Godot's "-col"/"-colonly" suffix.
bool IsCollisionMesh(const std::string& name) {
    const std::vector<std::string> tokens = Tokens(name);
    for (const std::string& token : tokens) {
        if (token == "ucx" || token == "ubx" || token == "usp" || token == "ucp" || token == "col" || token == "collision" ||
            token == "collider" || token == "collisions") {
            return true;
        }
    }
    return !tokens.empty() && tokens.size() > 1 && (tokens.back() == "col" || tokens.back() == "colonly");
}

struct StagingFolder {
    fs::path path;
    ~StagingFolder() {
        if (!path.empty()) {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    }
};

} // namespace

bool AssetIntake::ExtractZip(const fs::path& zip, const fs::path& destination, std::string& outError) {
    std::ifstream in(zip, std::ios::binary);
    if (!in) {
        outError = "Could not open " + zip.string();
        return false;
    }
    in.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(in.tellg());
    const size_t tail = static_cast<size_t>(std::min<uint64_t>(size, 22 + 65535));
    std::vector<uint8_t> buffer;
    if (size < 22 || !ReadAt(in, size - tail, tail, buffer)) {
        outError = "Not a ZIP archive: " + zip.string();
        return false;
    }
    size_t eocd = std::string::npos;
    for (size_t i = tail - 22 + 1; i-- > 0;) {
        if (U32(&buffer[i]) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        outError = "Not a ZIP archive: " + zip.string();
        return false;
    }
    const uint16_t entries = U16(&buffer[eocd + 10]);
    const uint32_t directorySize = U32(&buffer[eocd + 12]);
    const uint32_t directoryOffset = U32(&buffer[eocd + 16]);
    if (entries == 0xFFFF || directoryOffset == 0xFFFFFFFFu) {
        outError = "This archive uses ZIP64. Extract it first, then import the folder.";
        return false;
    }
    std::vector<uint8_t> directory;
    if (!ReadAt(in, directoryOffset, directorySize, directory)) {
        outError = "The ZIP archive is truncated.";
        return false;
    }
    std::error_code ec;
    fs::create_directories(destination, ec);
    size_t pos = 0;
    for (uint16_t entry = 0; entry < entries; ++entry) {
        if (pos + 46 > directory.size() || U32(&directory[pos]) != 0x02014b50) {
            outError = "The ZIP archive's directory is damaged.";
            return false;
        }
        const uint16_t flags = U16(&directory[pos + 8]);
        const uint16_t method = U16(&directory[pos + 10]);
        const uint32_t packed = U32(&directory[pos + 20]);
        const uint32_t unpacked = U32(&directory[pos + 24]);
        const uint16_t nameLength = U16(&directory[pos + 28]);
        const size_t next = pos + 46 + nameLength + U16(&directory[pos + 30]) + U16(&directory[pos + 32]);
        const uint32_t localOffset = U32(&directory[pos + 42]);
        if (pos + 46 + nameLength > directory.size()) {
            outError = "The ZIP archive's directory is damaged.";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(&directory[pos + 46]), nameLength);
        pos = next;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.back() == '/') {
            continue;
        }
        if (flags & 1u) {
            outError = "Encrypted ZIP archives are not supported.";
            return false;
        }
        if (packed == 0xFFFFFFFFu || unpacked == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu) {
            outError = "This archive uses ZIP64. Extract it first, then import the folder.";
            return false;
        }
        if (unpacked > static_cast<uint32_t>(INT_MAX) || packed > static_cast<uint32_t>(INT_MAX)) {
            outError = "A file in the archive is too large to extract here. Extract it first, then import the folder.";
            return false;
        }
        const fs::path relative = fs::path(std::u8string(name.begin(), name.end())).lexically_normal();
        const fs::path target = (destination / relative).lexically_normal();
        if (relative.is_absolute() || relative.has_root_name() || !IsUnderFolder(destination, target)) {
            outError = "The archive contains an unsafe path: " + name;
            return false;
        }
        std::vector<uint8_t> local;
        if (!ReadAt(in, localOffset, 30, local) || U32(local.data()) != 0x04034b50) {
            outError = "The ZIP archive is damaged at " + name;
            return false;
        }
        const uint64_t dataOffset = static_cast<uint64_t>(localOffset) + 30 + U16(&local[26]) + U16(&local[28]);
        std::vector<uint8_t> data;
        if (!ReadAt(in, dataOffset, packed, data)) {
            outError = "The ZIP archive is truncated at " + name;
            return false;
        }
        std::vector<uint8_t> output;
        if (method == 0) {
            output = std::move(data);
        } else if (method == 8) {
            output.resize(unpacked);
            const int written = unpacked == 0 ? 0
                : stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(output.data()), static_cast<int>(unpacked),
                                                   reinterpret_cast<const char*>(data.data()),
                                                   static_cast<int>(packed));
            if (written != static_cast<int>(unpacked)) {
                outError = "Could not decompress " + name;
                return false;
            }
        } else {
            outError = "Unsupported ZIP compression method " + std::to_string(method) + " for " + name;
            return false;
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
        if (!out) {
            outError = "Could not write " + target.string();
            return false;
        }
    }
    return true;
}

bool AssetIntake::Import(const AssetIntakeRequest& request, AssetIntakeResult& outResult, std::string& outError) {
    outResult = AssetIntakeResult{};
    std::error_code ec;
    const fs::path projectRoot = request.projectRoot;
    if (projectRoot.empty() || !fs::is_directory(projectRoot, ec)) {
        outError = "Open a project before importing assets.";
        return false;
    }
    const fs::path relativeTarget = fs::path(request.targetFolder).lexically_normal();
    if (relativeTarget.empty() || *relativeTarget.begin() != "assets" ||
        !IsUnderFolder(projectRoot / "assets", projectRoot / relativeTarget)) {
        outError = "Import folders must be inside the project's assets folder.";
        return false;
    }
    fs::path folder = projectRoot / relativeTarget;
    for (int suffix = 2; fs::exists(folder, ec); ++suffix) {
        folder = projectRoot / relativeTarget.parent_path() /
                 (relativeTarget.filename().string() + "_" + std::to_string(suffix));
    }

    StagingFolder staging;
    fs::path packRoot;
    fs::path singleModel;
    if (fs::is_directory(request.source, ec)) {
        packRoot = request.source;
    } else if (fs::is_regular_file(request.source, ec) && Ext(request.source) == ".zip") {
        staging.path = projectRoot / ".hephaestus" / "intake" /
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        if (!ExtractZip(request.source, staging.path, outError)) {
            return false;
        }
        packRoot = staging.path;
    } else if (fs::is_regular_file(request.source, ec) && IsModel(request.source)) {
        singleModel = request.source;
        packRoot = request.source.parent_path();
    } else {
        outError = "Choose a .zip, a folder, or an .fbx, .glb, .gltf, .obj or .usd file.";
        return false;
    }

    std::vector<fs::path> files;
    if (singleModel.empty()) {
        for (auto it = fs::recursive_directory_iterator(packRoot, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                files.push_back(it->path());
            }
        }
    } else {
        // Beside a single model only its folder and texture subfolders belong to it, never a whole Downloads folder.
        for (const fs::directory_entry& entry : fs::directory_iterator(packRoot, ec)) {
            const std::string name = Lower(entry.path().filename().string());
            if (entry.is_regular_file(ec)) {
                files.push_back(entry.path());
            } else if (entry.is_directory(ec) && (name == "textures" || name == "texture" || name == "maps")) {
                for (const fs::directory_entry& inner : fs::recursive_directory_iterator(entry.path(), ec)) {
                    if (inner.is_regular_file(ec)) {
                        files.push_back(inner.path());
                    }
                }
            }
        }
    }
    std::sort(files.begin(), files.end());

    std::vector<fs::path> models;
    std::vector<fs::path> images;
    std::vector<fs::path> extras;
    size_t unrealFiles = 0;
    size_t usdFiles = 0;
    for (const fs::path& file : files) {
        if (Usd::IsUsd(file)) {
            ++usdFiles; // a USD asset is spread over several layers; only its root file can be imported
        } else if (IsModel(file)) {
            if (singleModel.empty()) {
                models.push_back(file);
            }
        } else if (IsImage(file)) {
            images.push_back(file);
        } else if (IsUnreal(file)) {
            ++unrealFiles;
        } else if (IsUnsupportedImage(file) || IsDocument(file)) {
            extras.push_back(file);
        }
    }
    if (!singleModel.empty()) {
        models.push_back(singleModel);
    }

    // Megascans and Fab packs ship every LOD; LOD0 is the model, the others would only duplicate it.
    std::map<std::string, std::pair<int, fs::path>> lowestLod;
    std::vector<fs::path> plainModels;
    for (const fs::path& model : models) {
        const std::string stem = model.stem().string();
        const std::string lower = Lower(stem);
        const size_t lod = lower.rfind("_lod");
        if (lod != std::string::npos && lod + 4 < lower.size() &&
            std::all_of(lower.begin() + static_cast<std::ptrdiff_t>(lod + 4), lower.end(),
                        [](unsigned char c) { return std::isdigit(c); })) {
            const int level = std::stoi(lower.substr(lod + 4));
            const std::string key = model.parent_path().generic_string() + "/" + lower.substr(0, lod);
            auto [it, inserted] = lowestLod.try_emplace(key, level, model);
            if (!inserted && level < it->second.first) {
                it->second = {level, model};
            }
        } else {
            plainModels.push_back(model);
        }
    }
    size_t skippedLods = 0;
    for (auto& [key, lod] : lowestLod) {
        plainModels.push_back(lod.second);
    }
    skippedLods = models.size() - plainModels.size();
    // Sellers often ship the same model in several formats (and Fab's extra files repeat the export): keep one
    // copy per name, preferring FBX, then GLB, glTF and OBJ, then the shallowest folder.
    auto formatRank = [](const fs::path& model) {
        const std::string e = Ext(model);
        return e == ".fbx" ? 0 : e == ".glb" ? 1 : e == ".gltf" ? 2 : 3;
    };
    std::map<std::string, fs::path> byName;
    for (const fs::path& model : plainModels) {
        auto [it, inserted] = byName.try_emplace(Lower(model.stem().string()), model);
        const auto depth = [](const fs::path& p) { return std::distance(p.begin(), p.end()); };
        if (!inserted && (formatRank(model) < formatRank(it->second) ||
                          (formatRank(model) == formatRank(it->second) && depth(model) < depth(it->second)))) {
            it->second = model;
        }
    }
    const size_t duplicateModels = plainModels.size() - byName.size();
    models.clear();
    for (const auto& [name, model] : byName) {
        models.push_back(model);
    }
    std::sort(models.begin(), models.end());
    std::stable_sort(images.begin(), images.end(), [](const fs::path& a, const fs::path& b) {
        return std::distance(a.begin(), a.end()) < std::distance(b.begin(), b.end());
    });

    if (models.empty() && images.empty()) {
        outError = unrealFiles > 0
            ? "This pack only contains Unreal Engine assets (.uasset). Download it from Fab in a format such as "
              "FBX or glTF instead."
            : "The pack has no models (.fbx, .glb, .gltf, .obj) or textures to import.";
        return false;
    }
    if (usdFiles > 0 && singleModel.empty()) {
        outResult.warnings.push_back(std::to_string(usdFiles) + " USD files were skipped; import a USD asset's root "
                                     ".usd file on its own.");
    }
    if (unrealFiles > 0) {
        outResult.warnings.push_back(std::to_string(unrealFiles) + " Unreal-only files (.uasset, .umap) were skipped.");
    }
    if (duplicateModels > 0) {
        outResult.warnings.push_back(std::to_string(duplicateModels) +
                                     " copies of models in other formats or folders were skipped.");
    }
    if (skippedLods > 0) {
        outResult.warnings.push_back(std::to_string(skippedLods) + " lower-detail LOD models were skipped; LOD0 was imported.");
    }

    std::map<std::string, TextureSet> sets;
    for (const fs::path& image : images) {
        std::string base;
        bool directX = false;
        const MapKind kind = Classify(image.stem().string(), base, directX);
        if (kind != MapKind::None) {
            TextureSet& set = sets[base];
            if (set.displayName.empty()) {
                set.displayName = base;
            }
            AddToSet(set, kind, image, directX);
        }
    }

    // A USD kit's shared Textures folder only fills materials of exactly the same name.
    std::map<std::string, TextureSet> librarySets;
    if (!singleModel.empty() && Usd::IsUsd(singleModel)) {
        fs::path dir = singleModel.parent_path();
        for (int up = 0; up < 3 && dir.has_parent_path() && dir.parent_path() != dir; ++up) {
            dir = dir.parent_path();
            for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
                const std::string name = Lower(entry.path().filename().string());
                if (!entry.is_directory(ec) || (name != "textures" && name != "texture" && name != "maps")) {
                    continue;
                }
                std::vector<fs::path> library;
                for (const fs::directory_entry& inner : fs::recursive_directory_iterator(entry.path(), ec)) {
                    if (inner.is_regular_file(ec) && IsImage(inner.path())) {
                        library.push_back(inner.path());
                    }
                }
                std::sort(library.begin(), library.end());
                for (const fs::path& image : library) {
                    std::string base;
                    bool directX = false;
                    const MapKind kind = Classify(image.stem().string(), base, directX);
                    if (kind != MapKind::None) {
                        AddToSet(librarySets[base], kind, image, directX);
                    }
                }
            }
        }
    }
    auto librarySet = [&](const std::string& materialName) -> TextureSet* {
        const std::string key = Squash(MatchKey(materialName));
        for (auto& [setKey, set] : librarySets) {
            if (!key.empty() && Squash(setKey) == key) {
                return &set;
            }
        }
        return nullptr;
    };

    fs::create_directories(folder, ec);
    if (ec) {
        outError = "Could not create " + folder.string() + ": " + ec.message();
        return false;
    }
    outResult.folder = AssetPathPolicy::ToPortableKey(projectRoot, folder.generic_string());
    MaterialAssetWriter writer(projectRoot, folder / "Materials");
    std::set<std::string> materialStems;
    std::vector<fs::path> usedFiles;
    auto uniqueStem = [&](std::string stem) {
        stem = SanitizeAssetName(stem);
        std::string candidate = stem;
        for (int n = 2; !materialStems.insert(Lower(candidate)).second; ++n) {
            candidate = stem + "_" + std::to_string(n);
        }
        return candidate;
    };
    auto writeMaterial = [&](const SourceMaterial& material, const std::string& stem) {
        std::string error;
        const std::string path = writer.Write(material, uniqueStem(stem), error);
        if (path.empty()) {
            outResult.warnings.push_back(error);
        } else {
            outResult.materials.push_back(path);
        }
        return path;
    };

    // Materials for one mesh's slots, written once per model and material name.
    std::map<std::string, std::string> writtenMaterials;
    auto materialsFor = [&](const MeshData& mesh, const fs::path& model, const std::string& modelStem,
                            const std::vector<int>& slotMaterials, const std::vector<SourceMaterial>& gltfMaterials,
                            const std::vector<FbxMaterialInfo>& fbxMaterials) {
        const bool gltf = Ext(model) == ".gltf" || Ext(model) == ".glb" || Usd::IsUsd(model);
        const size_t slots = std::max<size_t>(mesh.submeshes.size(), 1);
        std::vector<std::string> applied;
        for (size_t s = 0; s < slots; ++s) {
            SourceMaterial material;
            if (gltf) {
                const int index = s < slotMaterials.size() ? slotMaterials[s] : -1;
                if (index >= 0 && index < static_cast<int>(gltfMaterials.size())) {
                    material = gltfMaterials[static_cast<size_t>(index)];
                }
            } else if (s < mesh.submeshes.size()) {
                const auto found = std::find_if(fbxMaterials.begin(), fbxMaterials.end(), [&](const FbxMaterialInfo& m) {
                    return m.name == mesh.submeshes[s].name;
                });
                if (found != fbxMaterials.end()) {
                    material = SourceMaterialFromFbx(*found);
                }
            }
            if (material.name.empty()) {
                material.name = s < mesh.submeshes.size() && !mesh.submeshes[s].name.empty()
                    ? mesh.submeshes[s].name : modelStem;
            }
            const std::string cacheKey = modelStem + "/" + material.name;
            if (const auto done = writtenMaterials.find(cacheKey); done != writtenMaterials.end()) {
                applied.push_back(done->second);
                continue;
            }
            TextureSet* set = FindSet(sets, material.name);
            if (!set) {
                set = FindSet(sets, modelStem);
            }
            if (!set && sets.size() == 1) {
                set = &sets.begin()->second;
            }
            if (!set) {
                set = librarySet(material.name);
            }
            if (set) {
                FillFromSet(material, *set);
            }
            const std::string stem = models.size() == 1 ? material.name : modelStem + "_" + material.name;
            const std::string path = writeMaterial(material, stem);
            writtenMaterials.emplace(cacheKey, path);
            applied.push_back(path);
        }
        return applied;
    };

    struct PrefabPart {
        std::string name;
        std::string meshPath;
        AABB bounds;
        int submeshCount = 0;
        std::vector<std::string> materials;
        Vec3 position{0.0f, 0.0f, 0.0f};
        bool collision = false; ///< hidden mesh used only as the prefab's static collider
        std::string group; ///< parent node under the root, created once per name; empty for the root itself
        Vec3 groupPosition{0.0f, 0.0f, 0.0f};
    };
    // One prefab per model: a root node with a child per mesh (one child unless an FBX was split into its meshes).
    auto writePrefab = [&](const std::string& modelStem, const std::vector<PrefabPart>& parts) {
        PrefabAsset prefab;
        prefab.guid = PrefabOverrideSystem::GenerateId();
        prefab.name = SanitizeAssetName(modelStem);
        prefab.rootNodeId = "node_0";
        PrefabNode root;
        root.id = "node_0";
        root.name = prefab.name;
        prefab.nodes.push_back(root);
        std::map<std::string, std::string> groups;
        for (size_t i = 0; i < parts.size(); ++i) {
            const PrefabPart& part = parts[i];
            std::string parentId = root.id;
            if (!part.group.empty()) {
                auto [group, created] = groups.try_emplace(part.group, "group_" + std::to_string(groups.size() + 1));
                if (created) {
                    PrefabNode groupNode;
                    groupNode.id = group->second;
                    groupNode.name = SanitizeAssetName(part.group);
                    groupNode.parentId = root.id;
                    groupNode.localTransform.position = part.groupPosition;
                    prefab.nodes.push_back(std::move(groupNode));
                }
                parentId = group->second;
            }
            MeshRendererComponent renderer(part.bounds);
            renderer.meshAssetPath = part.meshPath;
            renderer.meshImported = true;
            renderer.submeshCount = part.submeshCount;
            renderer.appliedMaterialPaths = part.materials;
            if (part.collision) {
                renderer.visible = false;
                renderer.castShadows = false;
            }
            PrefabNode meshNode;
            meshNode.id = "node_" + std::to_string(i + 1);
            meshNode.name = parts.size() == 1 ? prefab.name + "_Mesh" : SanitizeAssetName(part.name);
            meshNode.parentId = parentId;
            meshNode.localTransform.position = part.position;
            meshNode.components.push_back({"MeshRenderer", true, PrefabComponentIO::MeshRendererToJson(renderer)});
            if (part.collision) {
                MeshColliderComponent collider;
                collider.isStatic = true;
                collider.useComplexCollision = true;
                meshNode.components.push_back(
                    {"MeshCollider", true, PrefabComponentIO::MeshColliderToJson(collider)});
            }
            prefab.nodes.push_back(std::move(meshNode));
        }
        fs::path prefabPath = folder / "Prefabs" / (prefab.name + ".hpprefab");
        for (int n = 2; fs::exists(prefabPath, ec); ++n) {
            prefabPath = folder / "Prefabs" / (prefab.name + "_" + std::to_string(n) + ".hpprefab");
        }
        fs::create_directories(prefabPath.parent_path(), ec);
        std::string error;
        if (!PrefabSerializer::Serialize(prefab, prefabPath, error)) {
            outResult.warnings.push_back(error);
            return;
        }
        outResult.prefabs.push_back(AssetPathPolicy::ToPortableKey(projectRoot, prefabPath.generic_string()));
    };

    for (const fs::path& model : models) {
        std::string error;
        const std::string modelStem = model.stem().string();
        const bool gltf = Ext(model) == ".gltf" || Ext(model) == ".glb";
        std::vector<SourceMaterial> gltfMaterials;
        std::vector<FbxMaterialInfo> fbxMaterials;
        if (gltf && !ReadGltfMaterials(model, gltfMaterials, error)) {
            outResult.warnings.push_back(model.filename().string() + ": " + error);
        } else if (Ext(model) == ".fbx" && !ReadFbxMaterials(model, fbxMaterials, error)) {
            outResult.warnings.push_back(model.filename().string() + ": " + error);
        }

        // An FBX holding several meshes (a kit of pieces) becomes one model per mesh, each placed where it was.
        if (Ext(model) == ".fbx") {
            std::vector<FbxMeshPart> fbxParts;
            FbxStaticImportResult meta;
            if (FbxStaticImporter::LoadParts(model, FbxSpaceConversion::Auto, fbxParts, meta, error) &&
                fbxParts.size() > 1) {
                std::vector<PrefabPart> parts;
                const fs::path partFolder = folder / "Models" / SanitizeAssetName(modelStem);
                fs::create_directories(partFolder, ec);
                for (const FbxMeshPart& fbxPart : fbxParts) {
                    std::vector<std::string> names;
                    for (const MeshData::Submesh& submesh : fbxPart.mesh.submeshes) {
                        names.push_back(submesh.name);
                    }
                    const fs::path partFile = partFolder / (SanitizeAssetName(fbxPart.name) + ".glb");
                    if (!Gltf::WriteGlb(partFile, fbxPart.mesh, names, error)) {
                        outResult.warnings.push_back(error);
                        continue;
                    }
                    PrefabPart part;
                    part.name = fbxPart.name;
                    part.meshPath = AssetPathPolicy::ToPortableKey(projectRoot, partFile.generic_string());
                    part.bounds = fbxPart.mesh.bounds;
                    part.submeshCount = static_cast<int>(fbxPart.mesh.submeshes.size());
                    part.collision = IsCollisionMesh(fbxPart.name);
                    if (!part.collision) {
                        part.materials = materialsFor(fbxPart.mesh, model, modelStem, {}, gltfMaterials, fbxMaterials);
                    }
                    part.position = fbxPart.origin;
                    if (!MeshSettings::SaveDefaultMaterials(partFile, part.materials, error)) {
                        outResult.warnings.push_back(error);
                    }
                    outResult.models.push_back(part.meshPath);
                    parts.push_back(std::move(part));
                }
                usedFiles.push_back(model);
                outResult.warnings.push_back(model.filename().string() + " held " + std::to_string(fbxParts.size()) +
                                             " meshes; each was imported as its own model.");
                writePrefab(modelStem, parts);
                continue;
            }
        }

        // The project keeps GLBs, so games need no USD reader. Each Mesh prim is its own model; identical ones
        // (instances of one prototype) share a file.
        if (Usd::IsUsd(model)) {
            std::vector<Usd::Part> usdParts;
            if (!Usd::LoadParts(model, usdParts, gltfMaterials, error)) {
                outResult.warnings.push_back(error);
                continue;
            }
            std::vector<PrefabPart> parts;
            std::map<std::string, std::string> written; // geometry and slots -> model path
            std::set<std::string> fileNames;
            for (const Usd::Part& usdPart : usdParts) {
                std::string key(reinterpret_cast<const char*>(usdPart.mesh.vertices.data()),
                                usdPart.mesh.vertices.size() * sizeof(Vertex));
                key.append(reinterpret_cast<const char*>(usdPart.mesh.indices.data()),
                           usdPart.mesh.indices.size() * sizeof(uint32_t));
                key.append(reinterpret_cast<const char*>(usdPart.slotMaterials.data()),
                           usdPart.slotMaterials.size() * sizeof(int));
                PrefabPart part;
                part.name = usdPart.name;
                part.group = usdPart.group;
                part.bounds = usdPart.mesh.bounds;
                part.submeshCount = static_cast<int>(usdPart.mesh.submeshes.size());
                part.collision = usdPart.collision || IsCollisionMesh(usdPart.name);
                if (part.collision && part.group.empty()) {
                    part.group = "Collision";
                }
                if (!part.collision) {
                    part.materials = materialsFor(usdPart.mesh, model, modelStem, usdPart.slotMaterials,
                                                  gltfMaterials, fbxMaterials);
                }
                part.groupPosition = usdPart.groupOrigin;
                part.position = usdParts.size() == 1 ? Vec3(0.0f, 0.0f, 0.0f)
                    : Vec3(usdPart.origin.x - usdPart.groupOrigin.x, usdPart.origin.y - usdPart.groupOrigin.y,
                           usdPart.origin.z - usdPart.groupOrigin.z);
                if (const auto found = written.find(key); found != written.end()) {
                    part.meshPath = found->second;
                    parts.push_back(std::move(part));
                    continue;
                }
                std::string fileStem = SanitizeAssetName(usdPart.name);
                for (int n = 2; !fileNames.insert(Lower(fileStem)).second; ++n) {
                    fileStem = SanitizeAssetName(usdPart.name) + "_" + std::to_string(n);
                }
                const fs::path file = usdParts.size() == 1
                    ? folder / "Models" / (SanitizeAssetName(modelStem) + ".glb")
                    : folder / "Models" / SanitizeAssetName(modelStem) / (fileStem + ".glb");
                fs::create_directories(file.parent_path(), ec);
                std::vector<std::string> names;
                for (const MeshData::Submesh& submesh : usdPart.mesh.submeshes) {
                    names.push_back(submesh.name);
                }
                MeshData mesh = usdPart.mesh;
                if (usdParts.size() == 1) {
                    for (Vertex& vertex : mesh.vertices) { // a lone part keeps the file's own pivot
                        vertex.pos[0] += usdPart.origin.x;
                        vertex.pos[1] += usdPart.origin.y;
                        vertex.pos[2] += usdPart.origin.z;
                    }
                    const Vec3 o = usdPart.origin;
                    auto shift = [&](const AABB& b) {
                        return AABB(Vec3(b.min.x + o.x, b.min.y + o.y, b.min.z + o.z),
                                    Vec3(b.max.x + o.x, b.max.y + o.y, b.max.z + o.z));
                    };
                    mesh.bounds = shift(mesh.bounds);
                    for (MeshData::Submesh& submesh : mesh.submeshes) {
                        submesh.bounds = shift(submesh.bounds);
                    }
                    part.bounds = mesh.bounds;
                }
                if (!Gltf::WriteGlb(file, mesh, names, error)) {
                    outResult.warnings.push_back(error);
                    continue;
                }
                part.meshPath = AssetPathPolicy::ToPortableKey(projectRoot, file.generic_string());
                written.emplace(key, part.meshPath);
                outResult.models.push_back(part.meshPath);
                if (!MeshSettings::SaveDefaultMaterials(file, part.materials, error)) {
                    outResult.warnings.push_back(error);
                }
                parts.push_back(std::move(part));
            }
            usedFiles.push_back(model);
            writePrefab(modelStem, parts);
            continue;
        }

        // Collision nodes in a glTF file become their own hidden collider; the visible mesh is rewritten without them.
        if (gltf) {
            MeshData visible;
            MeshData collision;
            std::vector<int> visibleSlots;
            std::string collisionError;
            const bool hasCollision =
                ModelImporter::LoadGLTF(model, collision, collisionError, nullptr, IsCollisionMesh) &&
                ModelImporter::LoadGLTF(model, visible, error, &visibleSlots,
                                        [](const std::string& name) { return !IsCollisionMesh(name); });
            if (hasCollision) {
                const fs::path base = folder / "Models" / SanitizeAssetName(modelStem);
                std::vector<PrefabPart> parts;
                for (const auto& [mesh, suffix, slots] :
                     {std::tuple<const MeshData&, const char*, const std::vector<int>*>{visible, "", &visibleSlots},
                      std::tuple<const MeshData&, const char*, const std::vector<int>*>{collision, "_Collision",
                                                                                         nullptr}}) {
                    const fs::path file = fs::path(base.string() + suffix + ".glb");
                    fs::create_directories(file.parent_path(), ec);
                    std::vector<std::string> names;
                    for (const MeshData::Submesh& submesh : mesh.submeshes) {
                        names.push_back(submesh.name);
                    }
                    if (!Gltf::WriteGlb(file, mesh, names, error)) {
                        outResult.warnings.push_back(error);
                        continue;
                    }
                    PrefabPart part;
                    part.name = modelStem + suffix;
                    part.collision = slots == nullptr;
                    part.meshPath = AssetPathPolicy::ToPortableKey(projectRoot, file.generic_string());
                    part.bounds = mesh.bounds;
                    part.submeshCount = static_cast<int>(mesh.submeshes.size());
                    if (!part.collision) {
                        part.materials = materialsFor(mesh, model, modelStem, *slots, gltfMaterials, fbxMaterials);
                        if (!MeshSettings::SaveDefaultMaterials(file, part.materials, error)) {
                            outResult.warnings.push_back(error);
                        }
                    }
                    outResult.models.push_back(part.meshPath);
                    parts.push_back(std::move(part));
                }
                usedFiles.push_back(model);
                writePrefab(modelStem, parts);
                continue;
            }
        }

        fs::path copied;
        if (!CopyModel(model, folder / "Models", copied, usedFiles, error)) {
            outResult.warnings.push_back(error);
            continue;
        }
        usedFiles.push_back(model);
        MeshData mesh;
        std::vector<int> slotMaterials;
        const bool loaded = gltf ? ModelImporter::LoadGLTF(copied, mesh, error, &slotMaterials)
                                 : ModelImporter::LoadModel(copied, mesh, error);
        if (!loaded) {
            outResult.warnings.push_back(model.filename().string() + ": " + error);
            continue;
        }
        PrefabPart part;
        part.name = modelStem;
        part.meshPath = AssetPathPolicy::ToPortableKey(projectRoot, copied.generic_string());
        part.bounds = mesh.bounds;
        part.submeshCount = static_cast<int>(mesh.submeshes.size());
        part.materials = materialsFor(mesh, model, modelStem, slotMaterials, gltfMaterials, fbxMaterials);
        outResult.models.push_back(part.meshPath);
        // The model keeps the materials too, so dropping the mesh itself into a scene is textured.
        if (!MeshSettings::SaveDefaultMaterials(copied, part.materials, error)) {
            outResult.warnings.push_back(error);
        }
        writePrefab(modelStem, {part});
    }

    // Texture sets no model used are surface materials (Megascans surfaces, trim sheets).
    for (auto& [key, set] : sets) {
        if (!set.used) {
            SourceMaterial material;
            material.name = set.displayName;
            FillFromSet(material, set);
            writeMaterial(material, set.displayName);
        }
    }

    std::set<std::string> consumed;
    for (const auto& [key, written] : writer.Written()) {
        consumed.insert(key.substr(0, key.find('|')));
    }
    for (const fs::path& used : usedFiles) {
        consumed.insert(fs::absolute(used).lexically_normal().generic_string());
    }
    for (const fs::path& file : images) {
        extras.push_back(file);
    }
    for (const fs::path& file : extras) {
        if (consumed.count(fs::absolute(file).lexically_normal().generic_string())) {
            continue;
        }
        const fs::path target = folder / (IsDocument(file) ? fs::path() : fs::path("Textures")) / file.filename();
        std::string error;
        if (fs::exists(target, ec) || !CopyInto(file, target, error)) {
            if (!error.empty()) {
                outResult.warnings.push_back(error);
            }
            continue;
        }
        outResult.otherFiles.push_back(AssetPathPolicy::ToPortableKey(projectRoot, target.generic_string()));
        if (IsUnsupportedImage(file)) {
            outResult.warnings.push_back(file.filename().string() +
                                         " was copied but the renderer cannot load its format.");
        }
    }

    if (!outResult.materials.empty() && writer.Written().empty()) {
        outResult.warnings.push_back("The pack has no textures (none in its files or embedded in its models), so its "
                                     "materials are plain colours. Fab listings may keep textures in additional files "
                                     "or another format.");
    }

    if (outResult.prefabs.empty() && outResult.materials.empty()) {
        fs::remove_all(folder, ec);
        outError = outResult.warnings.empty() ? "Nothing in the pack could be imported."
                                              : "Nothing in the pack could be imported: " + outResult.warnings.front();
        return false;
    }

    {
        // Where the pack came from, so Reimport can bring in a newer version of it.
        const json record = {{"schema", kImportSchema},
                             {"source", fs::absolute(request.source).lexically_normal().generic_string()}};
        std::ofstream out(folder / kImportFile, std::ios::trunc);
        out << record.dump(2) << '\n';
    }

    const AssetCredit& credit = request.credit;
    if (!credit.title.empty() || !credit.author.empty() || !credit.license.empty()) {
        const json record = {
            {"schema", kCreditSchema}, {"title", credit.title},       {"source", credit.source},
            {"sourceUrl", credit.sourceUrl}, {"author", credit.author}, {"authorUrl", credit.authorUrl},
            {"license", credit.license}, {"licenseUrl", credit.licenseUrl},
        };
        std::ofstream out(folder / kCreditFile, std::ios::trunc);
        out << record.dump(2) << '\n';
        out.close();
        std::string error;
        if (!out || !RegenerateCredits(projectRoot, error)) {
            outResult.warnings.push_back(error.empty() ? "Could not write the asset's credits.json." : error);
        }
    }
    return true;
}

fs::path AssetIntake::RecordedSource(const fs::path& projectRoot, const std::string& folder) {
    try {
        std::ifstream in(projectRoot / fs::path(folder) / kImportFile);
        if (!in) {
            return {};
        }
        const json record = json::parse(in);
        if (record.value("schema", "") != kImportSchema) {
            return {};
        }
        return fs::path(record.value("source", ""));
    } catch (const std::exception&) {
        return {};
    }
}

namespace {

// The new prefab, keeping the old one's guid and the ids of nodes with the same names (in order, for repeated
// names), so overrides and links keyed by node id still apply.
PrefabAsset KeepNodeIds(const PrefabAsset& updated, const PrefabAsset& existing) {
    PrefabAsset out = updated;
    out.guid = existing.guid.empty() ? updated.guid : existing.guid;
    std::map<std::string, std::vector<std::string>> idsByName;
    std::set<std::string> oldIds;
    for (const PrefabNode& node : existing.nodes) {
        idsByName[node.name].push_back(node.id);
        oldIds.insert(node.id);
    }
    std::map<std::string, size_t> nextOfName;
    std::map<std::string, std::string> remap;
    std::set<std::string> used;
    for (const PrefabNode& node : updated.nodes) {
        auto ids = idsByName.find(node.name);
        size_t& next = nextOfName[node.name];
        if (ids != idsByName.end() && next < ids->second.size()) {
            remap[node.id] = ids->second[next++];
            used.insert(remap[node.id]);
        }
    }
    for (const PrefabNode& node : updated.nodes) {
        if (remap.count(node.id)) {
            continue;
        }
        // A new node must not take an old node's id, or that node's overrides would land on it.
        std::string id = node.id;
        while (oldIds.count(id) || used.count(id)) {
            id = PrefabOverrideSystem::GenerateId();
        }
        remap[node.id] = id;
        used.insert(id);
    }
    for (PrefabNode& node : out.nodes) {
        node.id = remap[node.id];
        if (!node.parentId.empty() && remap.count(node.parentId)) {
            node.parentId = remap[node.parentId];
        }
    }
    if (remap.count(out.rootNodeId)) {
        out.rootNodeId = remap[out.rootNodeId];
    }
    return out;
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

} // namespace

bool AssetIntake::Reimport(const fs::path& projectRoot, const std::string& folder, const fs::path& source,
                           AssetIntakeResult& outResult, std::string& outError) {
    outResult = AssetIntakeResult{};
    std::error_code ec;
    const fs::path target = projectRoot / fs::path(folder);
    if (folder.empty() || !fs::is_directory(target, ec) || !IsUnderFolder(projectRoot / "assets", target)) {
        outError = "Reimport needs an imported pack folder inside the project's assets folder.";
        return false;
    }
    const fs::path from = source.empty() ? RecordedSource(projectRoot, folder) : source;
    if (from.empty() || !fs::exists(from, ec)) {
        outError = from.empty() ? "This folder does not record where it was imported from; choose the file to reimport."
                                : "The pack's source is gone: " + from.string() + ". Choose the file to reimport.";
        return false;
    }

    // Import the new version beside the pack, then move it over the pack.
    AssetIntakeRequest request;
    request.source = from;
    request.projectRoot = projectRoot;
    request.targetFolder = (fs::path(folder).parent_path() / (fs::path(folder).filename().string() + "_reimport"))
                               .generic_string();
    try {
        std::ifstream in(target / kCreditFile);
        if (in) {
            const json credit = json::parse(in);
            request.credit = {credit.value("title", ""),     credit.value("source", ""), credit.value("sourceUrl", ""),
                              credit.value("author", ""),    credit.value("authorUrl", ""),
                              credit.value("license", ""),   credit.value("licenseUrl", "")};
        }
    } catch (const std::exception&) {
    }
    AssetIntakeResult fresh;
    if (!Import(request, fresh, outError)) {
        return false;
    }
    const fs::path staged = projectRoot / fs::path(fresh.folder);
    const std::string stagedKey = fresh.folder;
    const std::string targetKey = AssetPathPolicy::ToPortableKey(projectRoot, target.generic_string());
    const std::string stagedAbsolute = staged.generic_string();
    const std::string targetAbsolute = target.generic_string();
    auto retarget = [&](std::string text) {
        ReplaceAll(text, stagedAbsolute, targetAbsolute);
        ReplaceAll(text, stagedKey, targetKey);
        return text;
    };
    auto isText = [](const fs::path& file) {
        const std::string ext = Ext(file);
        return ext == ".hpprefab" || ext == ".hematerial" || ext == ".hpmesh" || ext == ".json" || ext == ".lua";
    };
    bool ok = true;
    for (auto it = fs::recursive_directory_iterator(staged, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const fs::path relative = it->path().lexically_relative(staged);
        const fs::path destination = target / relative;
        fs::create_directories(destination.parent_path(), ec);
        if (Ext(it->path()) == ".hpprefab" && fs::exists(destination, ec)) {
            PrefabAsset updated;
            PrefabAsset existing;
            std::string error;
            if (!PrefabSerializer::DeserializeFromString(updated, retarget([&] {
                    std::ifstream in(it->path(), std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(in), {});
                }()), error) ||
                !PrefabSerializer::Deserialize(existing, destination, error) ||
                !PrefabSerializer::Serialize(KeepNodeIds(updated, existing), destination, error)) {
                outResult.warnings.push_back(relative.generic_string() + ": " + error);
                ok = false;
            }
            continue;
        }
        if (isText(it->path())) {
            std::ifstream in(it->path(), std::ios::binary);
            const std::string text = retarget(std::string(std::istreambuf_iterator<char>(in), {}));
            std::ofstream out(destination, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) {
                outResult.warnings.push_back("Could not write " + destination.string());
                ok = false;
            }
            continue;
        }
        fs::copy_file(it->path(), destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outResult.warnings.push_back("Could not replace " + destination.string() + ": " + ec.message());
            ok = false;
            ec.clear();
        }
    }
    fs::remove_all(staged, ec);

    auto moved = [&](std::vector<std::string> keys) {
        for (std::string& key : keys) {
            ReplaceAll(key, stagedKey, targetKey);
        }
        return keys;
    };
    outResult.folder = targetKey;
    outResult.prefabs = moved(fresh.prefabs);
    outResult.models = moved(fresh.models);
    outResult.materials = moved(fresh.materials);
    outResult.otherFiles = moved(fresh.otherFiles);
    outResult.warnings.insert(outResult.warnings.begin(), fresh.warnings.begin(), fresh.warnings.end());
    std::string creditError;
    if (!RegenerateCredits(projectRoot, creditError) && !creditError.empty()) {
        outResult.warnings.push_back(creditError);
    }
    if (!ok) {
        outError = "Some files could not be updated: " + outResult.warnings.back();
    }
    return ok;
}

bool AssetIntake::RegenerateCredits(const fs::path& projectRoot, std::string& outError) {
    std::error_code ec;
    std::vector<json> records;
    const fs::path assets = projectRoot / "assets";
    for (auto it = fs::recursive_directory_iterator(assets, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().filename() != kCreditFile) {
            continue;
        }
        try {
            std::ifstream in(it->path());
            json record = json::parse(in);
            if (record.value("schema", "") == kCreditSchema) {
                records.push_back(std::move(record));
            }
        } catch (const std::exception&) {
            // A hand-edited record that no longer parses is left out until it is fixed.
        }
    }
    const fs::path output = assets / "scripts" / "ImportedCredits.lua";
    if (records.empty() && !fs::exists(output, ec)) {
        return true;
    }
    std::sort(records.begin(), records.end(), [](const json& a, const json& b) {
        return Lower(a.value("title", "")) < Lower(b.value("title", ""));
    });
    std::string lua =
        "-- Generated by the Hephaestus asset importers from the credits.json beside each imported asset.\n"
        "-- Edit those files instead: this file is rewritten on every import.\n"
        "return { heading = \"IMPORTED ASSETS\", entries = {\n";
    for (const json& record : records) {
        std::string note = record.value("sourceUrl", "");
        const std::string license = record.value("license", "");
        const std::string licenseUrl = record.value("licenseUrl", "");
        if (!license.empty()) {
            note += (note.empty() ? "" : " - ") + std::string("Licensed under ") + license +
                    (licenseUrl.empty() ? "" : " (" + licenseUrl + ")");
        }
        if (note.empty()) {
            note = record.value("source", "");
        }
        lua += "    { role = " + LuaString("\"" + Upper(record.value("title", "")) + "\"");
        const std::string author = record.value("author", "");
        if (!author.empty()) {
            lua += ", names = { " + LuaString(author) + " }";
        }
        if (!note.empty()) {
            lua += ", note = " + LuaString(note);
        }
        lua += " },\n";
    }
    lua += "} }\n";
    fs::create_directories(output.parent_path(), ec);
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    out << lua;
    if (!out) {
        outError = "Could not write " + output.string();
        return false;
    }
    return true;
}

} // namespace Hephaestus
