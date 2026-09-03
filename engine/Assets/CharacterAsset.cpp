#include "CharacterAsset.h"

#include "AssetPathPolicy.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Hephaestus {
namespace {

using json = nlohmann::json;

std::string NormalizeSeparators(const std::string& value) {
    return AssetPathPolicy::NormalizeSeparators(value);
}

std::string EscapeString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string FormatFloat(float value) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string JsonString(const std::string& value) {
    return "\"" + EscapeString(value) + "\"";
}

std::string PortableOrEmpty(const std::filesystem::path& projectRoot, const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const std::string portable = AssetPathPolicy::ToPortableKey(projectRoot, path);
    return portable.empty() ? NormalizeSeparators(path) : portable;
}

std::string ReadString(const json& obj, const char* key) {
    if (!obj.contains(key) || !obj[key].is_string()) {
        return {};
    }
    return obj[key].get<std::string>();
}

bool ReadBool(const json& obj, const char* key, bool fallback) {
    if (!obj.contains(key) || !obj[key].is_boolean()) {
        return fallback;
    }
    return obj[key].get<bool>();
}

float ReadFloat(const json& obj, const char* key, float fallback) {
    if (!obj.contains(key) || !obj[key].is_number()) {
        return fallback;
    }
    return obj[key].get<float>();
}

int ReadInt(const json& obj, const char* key, int fallback) {
    if (!obj.contains(key) || !obj[key].is_number_integer()) {
        if (!obj.contains(key) || !obj[key].is_number()) {
            return fallback;
        }
        return static_cast<int>(obj[key].get<double>());
    }
    return obj[key].get<int>();
}

bool ReadVec3(const json& value, float out[3]) {
    if (!value.is_array() || value.size() < 3) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!value[i].is_number()) {
            return false;
        }
        out[i] = value[i].get<float>();
    }
    return true;
}

std::string FileStem(const std::string& portablePath) {
    const std::string normalized = NormalizeSeparators(portablePath);
    const auto slash = normalized.find_last_of('/');
    const std::string file = (slash == std::string::npos) ? normalized : normalized.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return file;
    }
    return file.substr(0, dot);
}

std::string ParentPortable(const std::string& portablePath) {
    const std::string normalized = NormalizeSeparators(portablePath);
    const auto slash = normalized.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return normalized.substr(0, slash);
}

} // namespace

std::string CharacterOwnedSource::CharacterStem(const std::string& characterPortablePath) {
    return FileStem(characterPortablePath);
}

std::string CharacterOwnedSource::OwnedDirectoryPortableKey(const std::string& characterPortablePath) {
    const std::string parent = ParentPortable(NormalizeSeparators(characterPortablePath));
    const std::string stem = FileStem(characterPortablePath);
    if (stem.empty()) {
        return {};
    }
    if (parent.empty()) {
        return ".source/" + stem;
    }
    return parent + "/.source/" + stem;
}

std::string CharacterOwnedSource::ControllerScriptFileName(const std::string& characterStem) {
    return characterStem + "Character.lua";
}

bool CharacterOwnedSource::IsOwnedSourcePortableKey(const std::string& portablePath) {
    const std::string normalized = NormalizeSeparators(portablePath);
    return normalized.find("/.source/") != std::string::npos ||
           normalized.rfind(".source/", 0) == 0;
}

bool CharacterOwnedSource::IsCharacterAssetPortableKey(const std::string& portablePath) {
    const std::string normalized = NormalizeSeparators(portablePath);
    const auto dot = normalized.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = normalized.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".hpcharacter";
}

void CharacterOwnedSource::RewriteOwnedDirectoryKeys(CharacterAsset& asset,
                                                     const std::string& oldOwnedDir,
                                                     const std::string& newOwnedDir) {
    if (oldOwnedDir.empty() || oldOwnedDir == newOwnedDir) {
        return;
    }
    auto rewrite = [&](std::string& value) {
        if (value.size() < oldOwnedDir.size()) {
            return;
        }
        if (value.compare(0, oldOwnedDir.size(), oldOwnedDir) != 0) {
            return;
        }
        if (value.size() > oldOwnedDir.size() && value[oldOwnedDir.size()] != '/') {
            return;
        }
        value = newOwnedDir + value.substr(oldOwnedDir.size());
    };
    rewrite(asset.skeleton.source);
    rewrite(asset.mesh.source);
    rewrite(asset.controller.scriptPath);
    for (CharacterClipEntry& clip : asset.clips) {
        rewrite(clip.source);
    }
}

bool CharacterOwnedSource::RelocateAfterCharacterMove(const std::filesystem::path& projectRoot,
                                                      const std::string& oldCharacterPortable,
                                                      const std::string& newCharacterPortable,
                                                      std::string& outError) {
    outError.clear();
    if (!IsCharacterAssetPortableKey(oldCharacterPortable) ||
        !IsCharacterAssetPortableKey(newCharacterPortable)) {
        return true;
    }

    const std::string oldOwned = OwnedDirectoryPortableKey(oldCharacterPortable);
    const std::string newOwned = OwnedDirectoryPortableKey(newCharacterPortable);
    const std::string oldStem = CharacterStem(oldCharacterPortable);
    const std::string newStem = CharacterStem(newCharacterPortable);
    const std::filesystem::path oldOwnedAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, oldOwned);
    const std::filesystem::path newOwnedAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, newOwned);
    const std::filesystem::path characterAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, newCharacterPortable);

    std::error_code ec;
    if (!oldOwned.empty() && oldOwned != newOwned && !oldOwnedAbs.empty() &&
        std::filesystem::exists(oldOwnedAbs, ec)) {
        if (!newOwnedAbs.empty() && std::filesystem::exists(newOwnedAbs, ec) &&
            !std::filesystem::equivalent(oldOwnedAbs, newOwnedAbs, ec)) {
            outError = "Owned source directory already exists: " + newOwned;
            return false;
        }
        std::filesystem::create_directories(newOwnedAbs.parent_path(), ec);
        std::filesystem::rename(oldOwnedAbs, newOwnedAbs, ec);
        if (ec) {
            outError = "Failed to move owned source directory: " + ec.message();
            return false;
        }
    }

    if (oldStem != newStem && !newOwnedAbs.empty()) {
        const std::filesystem::path oldLua = newOwnedAbs / ControllerScriptFileName(oldStem);
        const std::filesystem::path newLua = newOwnedAbs / ControllerScriptFileName(newStem);
        if (std::filesystem::exists(oldLua, ec) && !std::filesystem::exists(newLua, ec)) {
            std::filesystem::rename(oldLua, newLua, ec);
            if (ec) {
                outError = "Failed to rename controller script: " + ec.message();
                return false;
            }
        }
    }

    if (characterAbs.empty() || !std::filesystem::exists(characterAbs, ec)) {
        return true;
    }

    CharacterAsset asset;
    if (!CharacterSerializer::Deserialize(asset, characterAbs, outError)) {
        return false;
    }
    RewriteOwnedDirectoryKeys(asset, oldOwned, newOwned);
    if (oldStem != newStem && !asset.controller.scriptPath.empty()) {
        const std::string oldLuaName = ControllerScriptFileName(oldStem);
        const std::string newLuaName = ControllerScriptFileName(newStem);
        if (asset.controller.scriptPath.size() >= oldLuaName.size() &&
            asset.controller.scriptPath.compare(
                asset.controller.scriptPath.size() - oldLuaName.size(),
                oldLuaName.size(), oldLuaName) == 0) {
            asset.controller.scriptPath.replace(
                asset.controller.scriptPath.size() - oldLuaName.size(),
                oldLuaName.size(), newLuaName);
        }
    }
    asset.name = newStem.empty() ? asset.name : newStem;
    return CharacterSerializer::Serialize(asset, characterAbs, outError);
}

CharacterAsset CharacterSerializer::MakeEmpty(const std::string& name) {
    CharacterAsset asset;
    asset.name = name;
    asset.controller.enabled = true;
    return asset;
}

bool CharacterSerializer::DeserializeFromString(CharacterAsset& asset,
                                                const std::string& data,
                                                std::string& outError) {
    json root;
    try {
        root = json::parse(data);
    } catch (const json::exception& ex) {
        outError = std::string("Malformed .hpcharacter JSON: ") + ex.what();
        return false;
    }

    if (!root.is_object()) {
        outError = ".hpcharacter root must be a JSON object";
        return false;
    }

    if (!root.contains("schema") || !root["schema"].is_string() ||
        root["schema"].get<std::string>() != CharacterAsset::kSchema) {
        outError = "Expected schema \"" + std::string(CharacterAsset::kSchema) + "\"";
        return false;
    }

    const int version = ReadInt(root, "assetVersion", 0);
    if (version < 1) {
        outError = "Missing or invalid assetVersion";
        return false;
    }
    if (version > CharacterAsset::kCurrentAssetVersion) {
        outError = "Unsupported .hpcharacter assetVersion " + std::to_string(version);
        return false;
    }

    asset = MakeEmpty(ReadString(root, "name"));
    asset.assetVersion = version;

    if (root.contains("import") && root["import"].is_object()) {
        const json& import = root["import"];
        asset.import.unitScale = ReadFloat(import, "unitScale", 1.0f);
        asset.import.preserveAuthoredEditsOnReimport =
            ReadBool(import, "preserveAuthoredEditsOnReimport", true);
    }

    if (root.contains("skeleton") && root["skeleton"].is_object()) {
        const json& skeleton = root["skeleton"];
        asset.skeleton.source = ReadString(skeleton, "source");
        asset.skeleton.skin = ReadString(skeleton, "skin");
        asset.skeleton.signature = ReadString(skeleton, "signature");
    }

    if (root.contains("mesh") && root["mesh"].is_object()) {
        const json& mesh = root["mesh"];
        asset.mesh.source = ReadString(mesh, "source");
        if (mesh.contains("submeshes") && mesh["submeshes"].is_array()) {
            for (const auto& entry : mesh["submeshes"]) {
                if (!entry.is_object()) {
                    continue;
                }
                CharacterSubmeshSlot slot;
                slot.name = ReadString(entry, "name");
                slot.material = ReadString(entry, "material");
                asset.mesh.submeshes.push_back(std::move(slot));
            }
        }
    }

    if (root.contains("animations") && root["animations"].is_object()) {
        const json& animations = root["animations"];
        if (animations.contains("folders") && animations["folders"].is_array()) {
            for (const auto& folder : animations["folders"]) {
                if (folder.is_string()) {
                    asset.animationFolders.push_back(folder.get<std::string>());
                }
            }
        }
        if (animations.contains("clips") && animations["clips"].is_array()) {
            for (const auto& clipJson : animations["clips"]) {
                if (!clipJson.is_object()) {
                    continue;
                }
                CharacterClipEntry clip;
                clip.id = ReadString(clipJson, "id");
                clip.displayName = ReadString(clipJson, "displayName");
                clip.folder = ReadString(clipJson, "folder");
                clip.source = ReadString(clipJson, "source");
                clip.sourceClip = ReadString(clipJson, "sourceClip");
                clip.loop = ReadBool(clipJson, "loop", true);
                clip.speed = ReadFloat(clipJson, "speed", 1.0f);
                clip.startTime = ReadFloat(clipJson, "startTime", 0.0f);
                clip.endTime = ReadFloat(clipJson, "endTime", 0.0f);
                asset.clips.push_back(std::move(clip));
            }
        }
    }

    if (root.contains("sockets") && root["sockets"].is_array()) {
        for (const auto& socketJson : root["sockets"]) {
            if (!socketJson.is_object()) {
                continue;
            }
            CharacterSocket socket;
            socket.name = ReadString(socketJson, "name");
            socket.bone = ReadString(socketJson, "bone");
            if (socketJson.contains("position")) {
                ReadVec3(socketJson["position"], socket.position);
            }
            if (socketJson.contains("rotation")) {
                ReadVec3(socketJson["rotation"], socket.rotation);
            }
            asset.sockets.push_back(socket);
        }
    }

    if (root.contains("controller") && root["controller"].is_object()) {
        const json& controller = root["controller"];
        asset.controller.scriptPath = ReadString(controller, "scriptPath");
        asset.controller.enabled = ReadBool(controller, "enabled", true);
    }

    if (root.contains("defaults") && root["defaults"].is_object()) {
        const json& defaults = root["defaults"];
        asset.defaults.animation = ReadString(defaults, "animation");
        asset.defaults.playbackSpeed = ReadFloat(defaults, "playbackSpeed", 1.0f);
    }

    if (root.contains("physics") && root["physics"].is_object()) {
        asset.physics.assetVersion = ReadInt(root["physics"], "assetVersion", 1);
    }

    if (root.contains("sharedRig") && root["sharedRig"].is_string()) {
        asset.sharedRig = root["sharedRig"].get<std::string>();
    }

    return true;
}

bool CharacterSerializer::Deserialize(CharacterAsset& asset,
                                      const std::filesystem::path& filepath,
                                      std::string& outError) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) {
        outError = "Failed to open " + filepath.generic_string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return DeserializeFromString(asset, ss.str(), outError);
}

std::string CharacterSerializer::SerializeToString(const CharacterAsset& asset) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema\": " << JsonString(CharacterAsset::kSchema) << ",\n";
    out << "  \"assetVersion\": " << asset.assetVersion << ",\n";
    out << "  \"name\": " << JsonString(asset.name) << ",\n";
    out << "  \"import\": {\n";
    out << "    \"unitScale\": " << FormatFloat(asset.import.unitScale) << ",\n";
    out << "    \"preserveAuthoredEditsOnReimport\": "
        << (asset.import.preserveAuthoredEditsOnReimport ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"skeleton\": {\n";
    out << "    \"source\": " << JsonString(asset.skeleton.source) << ",\n";
    out << "    \"skin\": " << JsonString(asset.skeleton.skin) << ",\n";
    out << "    \"signature\": " << JsonString(asset.skeleton.signature) << "\n";
    out << "  },\n";
    out << "  \"mesh\": {\n";
    out << "    \"source\": " << JsonString(asset.mesh.source) << ",\n";
    out << "    \"submeshes\": [\n";
    for (size_t i = 0; i < asset.mesh.submeshes.size(); ++i) {
        const auto& slot = asset.mesh.submeshes[i];
        out << "      {\n";
        out << "        \"name\": " << JsonString(slot.name) << ",\n";
        out << "        \"material\": " << JsonString(slot.material) << "\n";
        out << "      }";
        if (i + 1 < asset.mesh.submeshes.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"animations\": {\n";
    out << "    \"folders\": [";
    for (size_t i = 0; i < asset.animationFolders.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << JsonString(asset.animationFolders[i]);
    }
    out << "],\n";
    out << "    \"clips\": [\n";
    for (size_t i = 0; i < asset.clips.size(); ++i) {
        const auto& clip = asset.clips[i];
        out << "      {\n";
        out << "        \"id\": " << JsonString(clip.id) << ",\n";
        out << "        \"displayName\": " << JsonString(clip.displayName) << ",\n";
        out << "        \"folder\": " << JsonString(clip.folder) << ",\n";
        out << "        \"source\": " << JsonString(clip.source) << ",\n";
        out << "        \"sourceClip\": " << JsonString(clip.sourceClip) << ",\n";
        out << "        \"loop\": " << (clip.loop ? "true" : "false") << ",\n";
        out << "        \"speed\": " << FormatFloat(clip.speed) << ",\n";
        out << "        \"startTime\": " << FormatFloat(clip.startTime) << ",\n";
        out << "        \"endTime\": " << FormatFloat(clip.endTime) << "\n";
        out << "      }";
        if (i + 1 < asset.clips.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"sockets\": [\n";
    for (size_t i = 0; i < asset.sockets.size(); ++i) {
        const auto& socket = asset.sockets[i];
        out << "    {\n";
        out << "      \"name\": " << JsonString(socket.name) << ",\n";
        out << "      \"bone\": " << JsonString(socket.bone) << ",\n";
        out << "      \"position\": [" << FormatFloat(socket.position[0]) << ", "
            << FormatFloat(socket.position[1]) << ", " << FormatFloat(socket.position[2]) << "],\n";
        out << "      \"rotation\": [" << FormatFloat(socket.rotation[0]) << ", "
            << FormatFloat(socket.rotation[1]) << ", " << FormatFloat(socket.rotation[2]) << "]\n";
        out << "    }";
        if (i + 1 < asset.sockets.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"controller\": {\n";
    out << "    \"scriptPath\": " << JsonString(asset.controller.scriptPath) << ",\n";
    out << "    \"enabled\": " << (asset.controller.enabled ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"defaults\": {\n";
    out << "    \"animation\": " << JsonString(asset.defaults.animation) << ",\n";
    out << "    \"playbackSpeed\": " << FormatFloat(asset.defaults.playbackSpeed) << "\n";
    out << "  },\n";
    out << "  \"physics\": {\n";
    out << "    \"assetVersion\": " << asset.physics.assetVersion << ",\n";
    out << "    \"bodies\": []\n";
    out << "  },\n";
    if (asset.sharedRig.empty()) {
        out << "  \"sharedRig\": null\n";
    } else {
        out << "  \"sharedRig\": " << JsonString(asset.sharedRig) << "\n";
    }
    out << "}\n";
    return out.str();
}

bool CharacterSerializer::Serialize(const CharacterAsset& asset,
                                    const std::filesystem::path& filepath,
                                    std::string& outError) {
    std::error_code ec;
    std::filesystem::create_directories(filepath.parent_path(), ec);
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Failed to write " + filepath.generic_string();
        return false;
    }
    out << SerializeToString(asset);
    if (!out) {
        outError = "Failed while writing " + filepath.generic_string();
        return false;
    }
    return true;
}

bool CharacterSerializer::CreateNew(const std::filesystem::path& characterAbsolutePath,
                                    const std::filesystem::path& projectRoot,
                                    std::string& outError) {
    const std::string portable =
        AssetPathPolicy::ToPortableKey(projectRoot, characterAbsolutePath.generic_string());
    if (portable.empty()) {
        outError = "Character path is not a portable project asset";
        return false;
    }

    const std::string stem = CharacterOwnedSource::CharacterStem(portable);
    if (stem.empty()) {
        outError = "Character file name is empty";
        return false;
    }

    CharacterAsset asset = MakeEmpty(stem);
    const std::string ownedDir = CharacterOwnedSource::OwnedDirectoryPortableKey(portable);
    const std::string controllerName = CharacterOwnedSource::ControllerScriptFileName(stem);
    asset.controller.scriptPath = PortableOrEmpty(projectRoot, ownedDir + "/" + controllerName);
    asset.controller.enabled = true;

    const std::filesystem::path ownedAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, ownedDir);
    std::error_code ec;
    std::filesystem::create_directories(ownedAbs, ec);
    if (ec) {
        outError = "Failed to create owned source directory: " + ec.message();
        return false;
    }

    const std::filesystem::path luaAbs = ownedAbs / controllerName;
    if (!std::filesystem::exists(luaAbs)) {
        std::ofstream lua(luaAbs, std::ios::binary | std::ios::trunc);
        if (!lua) {
            outError = "Failed to write controller script " + luaAbs.generic_string();
            return false;
        }
        lua << "-- Character controller for " << stem << ".\n";
        lua << "-- Presentation only. Gameplay AI belongs on LuaScriptComponent.\n\n";
        lua << "function onAwake()\n";
        lua << "end\n\n";
        lua << "function onUpdate(dt)\n";
        lua << "end\n";
    }

    return Serialize(asset, characterAbsolutePath, outError);
}

} // namespace Hephaestus
