/**
 * @file AssetIntake.cpp
 * @brief Imports a downloaded asset pack into a project with its materials applied through prefabs.
 */

#include "AssetIntake.h"

#include "AssetPathPolicy.h"
#include "FbxDocument.h"
#include "ModelImporter.h"
#include "ModelMaterialImport.h"
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

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Hephaestus {
namespace {

constexpr const char* kCreditFile = "credits.json";
constexpr const char* kCreditSchema = "HephaestusAssetCredit";

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string Ext(const fs::path& path) { return Lower(path.extension().string()); }

bool IsModel(const fs::path& p) {
    const std::string e = Ext(p);
    return e == ".fbx" || e == ".glb" || e == ".gltf" || e == ".obj";
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
        outError = "Choose a .zip, a folder, or an .fbx, .glb, .gltf or .obj file.";
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
    for (const fs::path& file : files) {
        if (IsModel(file)) {
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

    for (const fs::path& model : models) {
        fs::path copied;
        std::string error;
        if (!CopyModel(model, folder / "Models", copied, usedFiles, error)) {
            outResult.warnings.push_back(error);
            continue;
        }
        usedFiles.push_back(model);
        const std::string modelStem = model.stem().string();
        MeshData mesh;
        std::vector<int> slotMaterials;
        const bool gltf = Ext(model) == ".gltf" || Ext(model) == ".glb";
        const bool loaded = gltf ? ModelImporter::LoadGLTF(copied, mesh, error, &slotMaterials)
                                 : ModelImporter::LoadModel(copied, mesh, error);
        if (!loaded) {
            outResult.warnings.push_back(model.filename().string() + ": " + error);
            continue;
        }
        const std::string modelPortable = AssetPathPolicy::ToPortableKey(projectRoot, copied.generic_string());
        outResult.models.push_back(modelPortable);

        const size_t slots = std::max<size_t>(mesh.submeshes.size(), 1);
        std::vector<SourceMaterial> slotSources(slots);
        if (gltf) {
            std::vector<SourceMaterial> materials;
            if (!ReadGltfMaterials(model, materials, error)) {
                outResult.warnings.push_back(model.filename().string() + ": " + error);
            }
            for (size_t s = 0; s < slots; ++s) {
                const int index = s < slotMaterials.size() ? slotMaterials[s] : -1;
                if (index >= 0 && index < static_cast<int>(materials.size())) {
                    slotSources[s] = materials[static_cast<size_t>(index)];
                }
            }
        } else if (Ext(model) == ".fbx") {
            std::vector<FbxMaterialInfo> materials;
            if (!ReadFbxMaterials(model, materials, error)) {
                outResult.warnings.push_back(model.filename().string() + ": " + error);
            }
            for (size_t s = 0; s < mesh.submeshes.size(); ++s) {
                const auto found = std::find_if(materials.begin(), materials.end(), [&](const FbxMaterialInfo& m) {
                    return m.name == mesh.submeshes[s].name;
                });
                if (found != materials.end()) {
                    slotSources[s] = SourceMaterialFromFbx(*found);
                }
            }
        }
        std::vector<std::string> applied;
        for (size_t s = 0; s < slots; ++s) {
            SourceMaterial& material = slotSources[s];
            if (material.name.empty()) {
                material.name = s < mesh.submeshes.size() && !mesh.submeshes[s].name.empty()
                    ? mesh.submeshes[s].name : modelStem;
            }
            TextureSet* set = FindSet(sets, material.name);
            if (!set) {
                set = FindSet(sets, modelStem);
            }
            if (!set && sets.size() == 1) {
                set = &sets.begin()->second;
            }
            if (set) {
                FillFromSet(material, *set);
            }
            const std::string stem = models.size() == 1 ? material.name : modelStem + "_" + material.name;
            applied.push_back(writeMaterial(material, stem));
        }

        MeshRendererComponent renderer(mesh.bounds);
        renderer.meshAssetPath = modelPortable;
        renderer.meshImported = true;
        renderer.submeshCount = static_cast<int>(mesh.submeshes.size());
        renderer.appliedMaterialPaths = applied;
        PrefabAsset prefab;
        prefab.guid = PrefabOverrideSystem::GenerateId();
        prefab.name = SanitizeAssetName(modelStem);
        prefab.rootNodeId = "node_0";
        PrefabNode root;
        root.id = "node_0";
        root.name = prefab.name;
        PrefabNode meshNode;
        meshNode.id = "node_1";
        meshNode.name = prefab.name + "_Mesh";
        meshNode.parentId = root.id;
        meshNode.components.push_back({"MeshRenderer", true, PrefabComponentIO::MeshRendererToJson(renderer)});
        prefab.nodes = {root, meshNode};
        fs::path prefabPath = folder / "Prefabs" / (prefab.name + ".hpprefab");
        for (int n = 2; fs::exists(prefabPath, ec); ++n) {
            prefabPath = folder / "Prefabs" / (prefab.name + "_" + std::to_string(n) + ".hpprefab");
        }
        fs::create_directories(prefabPath.parent_path(), ec);
        if (!PrefabSerializer::Serialize(prefab, prefabPath, error)) {
            outResult.warnings.push_back(error);
            continue;
        }
        outResult.prefabs.push_back(AssetPathPolicy::ToPortableKey(projectRoot, prefabPath.generic_string()));
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
