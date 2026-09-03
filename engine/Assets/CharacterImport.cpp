/**
 * @file CharacterImport.cpp
 * @brief GLB character inspect, import, and transactional reimport.
 */

#include "CharacterImport.h"

#include "AssetPathPolicy.h"
#include "CharacterRuntime.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace Hephaestus {
namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool EqualsInsensitive(const std::string& a, const std::string& b) {
    return ToLowerCopy(a) == ToLowerCopy(b);
}

std::string SanitizeCharacterName(std::string name) {
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    return name;
}

void WriteControllerStub(const std::filesystem::path& luaAbs, const std::string& stem) {
    if (std::filesystem::exists(luaAbs)) {
        return;
    }
    std::ofstream lua(luaAbs, std::ios::binary | std::ios::trunc);
    if (!lua) {
        return;
    }
    lua << "-- Character controller for " << stem << ".\n";
    lua << "-- Presentation only. Gameplay AI belongs on LuaScriptComponent.\n\n";
    lua << "function onAwake()\n";
    lua << "end\n\n";
    lua << "function onUpdate(dt)\n";
    lua << "end\n";
}

void CollectInspectWarnings(CharacterInspectResult& result) {
    result.warnings = result.imported.warnings;
    if (result.imported.clampedInfluences) {
        result.warnings.push_back("Vertex influences were clamped to 4.");
    }
    if (result.imported.restBindMismatch) {
        result.warnings.push_back("Rest pose and inverse-bind matrices do not match.");
    }
}

std::string FormatCompatibilityFailure(const SkeletonCompatibilityReport& report,
                                       const std::string& storedSignature,
                                       const std::string& sourceSignature,
                                       bool detailed) {
    std::string message =
        "Character reimport failed. The source skeleton is no longer compatible";
    if (detailed) {
        message += ".\n\n";
        if (!report.missingBones.empty()) {
            message += "Missing bones: ";
            for (size_t i = 0; i < report.missingBones.size(); ++i) {
                if (i != 0) {
                    message += ", ";
                }
                message += report.missingBones[i];
            }
            message += "\n";
        }
        if (!report.extraBones.empty()) {
            message += "Extra bones: ";
            for (size_t i = 0; i < report.extraBones.size(); ++i) {
                if (i != 0) {
                    message += ", ";
                }
                message += report.extraBones[i];
            }
            message += "\n";
        }
        if (!report.hierarchyMismatches.empty()) {
            message += "Hierarchy changes:\n";
            for (const std::string& line : report.hierarchyMismatches) {
                message += "  " + line + "\n";
            }
        }
        if (report.restPoseMismatch) {
            message += "Rest pose / bind identity changed.\n";
        }
    } else {
        message += " with this character.\n\n";
        message += "Stored signature: " + storedSignature + "\n";
        message += "Source signature: " + sourceSignature + "\n";
    }
    message += "\nThe existing character asset has not been modified.";
    return message;
}

void MergeClipsFromImport(CharacterAsset& asset,
                          const SkeletalImportResult& imported,
                          const std::string& meshPortable,
                          bool preserveAuthored) {
    std::unordered_map<std::string, CharacterClipEntry> previous;
    if (preserveAuthored) {
        for (const CharacterClipEntry& clip : asset.clips) {
            if (!clip.sourceClip.empty()) {
                previous[clip.sourceClip] = clip;
            }
        }
    }

    std::vector<CharacterClipEntry> merged;
    merged.reserve(imported.clips.size());
    for (const AnimationClip& clip : imported.clips) {
        CharacterClipEntry entry;
        const auto it = previous.find(clip.sourceClip.empty() ? clip.name : clip.sourceClip);
        if (it != previous.end()) {
            entry = it->second;
        } else {
            entry.id = clip.id.empty() ? clip.name : clip.id;
            entry.displayName = clip.name;
            entry.loop = true;
            entry.speed = 1.0f;
            entry.startTime = 0.0f;
            entry.endTime = clip.endTime > 0.0f ? clip.endTime : clip.duration;
        }
        entry.source = meshPortable;
        entry.sourceClip = clip.sourceClip.empty() ? clip.name : clip.sourceClip;
        if (entry.id.empty()) {
            entry.id = entry.sourceClip;
        }
        if (entry.displayName.empty()) {
            entry.displayName = entry.sourceClip;
        }
        merged.push_back(std::move(entry));
    }
    asset.clips = std::move(merged);
}

void MergeSubmeshesFromImport(CharacterAsset& asset, const SkeletalImportResult& imported) {
    std::unordered_map<std::string, std::string> materialsByName;
    for (const CharacterSubmeshSlot& slot : asset.mesh.submeshes) {
        if (!slot.name.empty()) {
            materialsByName[slot.name] = slot.material;
        }
    }
    asset.mesh.submeshes.clear();
    for (const SkinnedSubmeshData& src : imported.submeshes) {
        CharacterSubmeshSlot slot;
        slot.name = src.name.empty() ? ("Slot" + std::to_string(asset.mesh.submeshes.size()))
                                     : src.name;
        const auto it = materialsByName.find(slot.name);
        if (it != materialsByName.end()) {
            slot.material = it->second;
        }
        asset.mesh.submeshes.push_back(std::move(slot));
    }
}

bool AtomicWriteText(const std::filesystem::path& dest,
                     const std::string& contents,
                     std::string& outError) {
    const std::filesystem::path tmp = std::filesystem::path(dest.native() + std::filesystem::path(".tmp").native());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            outError = "Failed to write " + tmp.generic_string();
            return false;
        }
        out << contents;
        if (!out) {
            outError = "Failed while writing " + tmp.generic_string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        std::filesystem::remove(dest, ec);
        std::filesystem::rename(tmp, dest, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            outError = "Failed to replace " + dest.generic_string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

} // namespace

bool CharacterImporter::IsGlbPath(const std::filesystem::path& path) {
    return EqualsInsensitive(path.extension().string(), ".glb");
}

bool CharacterImporter::Inspect(const std::filesystem::path& sourceGlb,
                                CharacterInspectResult& outResult,
                                std::string& outError) {
    outResult = {};
    outError.clear();
    if (!IsGlbPath(sourceGlb)) {
        outError =
            "Import Character requires a GLB file. Export a GLB from the DCC tool "
            "(raw .gltf with external .bin / images is not supported).";
        outResult.error = outError;
        return false;
    }
    if (!std::filesystem::exists(sourceGlb)) {
        outError = "Source file not found.";
        outResult.error = outError;
        return false;
    }

    SkeletalImportSettings settings;
    if (!SkeletalModelImporter::Load(sourceGlb, settings, outResult.imported, outError)) {
        outResult.error = outError;
        return false;
    }

    outResult.ok = true;
    outResult.canImport = true;
    outResult.skinName = outResult.imported.skeleton.skinName;
    outResult.boneCount = outResult.imported.skeleton.BoneCount();
    outResult.clipCount = static_cast<int>(outResult.imported.clips.size());
    outResult.materialSlotCount = static_cast<int>(outResult.imported.submeshes.size());
    if (!outResult.imported.submeshes.empty()) {
        outResult.meshName = outResult.imported.submeshes.front().name;
    }
    if (outResult.meshName.empty()) {
        outResult.meshName = sourceGlb.stem().string();
    }
    CollectInspectWarnings(outResult);
    return true;
}

CharacterAsset CharacterImporter::BuildAssetFromImport(const std::string& name,
                                                       const std::string& meshPortable,
                                                       const SkeletalImportResult& imported,
                                                       bool createController,
                                                       const std::string& controllerPortable) {
    CharacterAsset asset = CharacterSerializer::MakeEmpty(name);
    asset.skeleton.source = meshPortable;
    asset.skeleton.skin = imported.skeleton.skinName;
    asset.skeleton.signature = imported.skeleton.signature;
    asset.mesh.source = meshPortable;
    MergeSubmeshesFromImport(asset, imported);
    MergeClipsFromImport(asset, imported, meshPortable, false);
    asset.defaults.animation = ChooseDefaultAnimation(asset.clips);
    asset.defaults.playbackSpeed = 1.0f;
    asset.controller.enabled = createController;
    asset.controller.scriptPath = createController ? controllerPortable : std::string();
    return asset;
}

bool CharacterImporter::Import(const CharacterImportRequest& request,
                               std::string& outPortableCharacterPath,
                               std::string& outError) {
    outPortableCharacterPath.clear();
    outError.clear();

    const std::string name = SanitizeCharacterName(request.characterName);
    if (name.empty()) {
        outError = "Character name is empty.";
        return false;
    }

    CharacterInspectResult inspect;
    if (!Inspect(request.sourceGlb, inspect, outError)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(request.destinationFolderAbs, ec);
    const std::filesystem::path characterAbs =
        request.destinationFolderAbs / (name + ".hpcharacter");
    if (std::filesystem::exists(characterAbs, ec)) {
        outError = "A character named \"" + name + "\" already exists in this folder.";
        return false;
    }

    const std::string portable =
        AssetPathPolicy::ToPortableKey(request.projectRoot, characterAbs.generic_string());
    if (portable.empty()) {
        outError = "Destination is not a portable project asset path.";
        return false;
    }

    const std::string ownedDir = CharacterOwnedSource::OwnedDirectoryPortableKey(portable);
    const std::filesystem::path ownedAbs =
        AssetPathPolicy::ResolvePortableKey(request.projectRoot, ownedDir);
    if (ownedAbs.empty()) {
        outError = "Failed to resolve owned source directory.";
        return false;
    }
    if (std::filesystem::exists(ownedAbs, ec)) {
        outError = "Owned source folder already exists for this character name.";
        return false;
    }

    std::filesystem::create_directories(ownedAbs, ec);
    if (ec) {
        outError = "Failed to create owned source directory: " + ec.message();
        return false;
    }

    const std::filesystem::path meshAbs = ownedAbs / "mesh.glb";
    std::filesystem::copy_file(request.sourceGlb, meshAbs,
                               std::filesystem::copy_options::none, ec);
    if (ec) {
        std::filesystem::remove_all(ownedAbs, ec);
        outError = "Failed to copy GLB: " + ec.message();
        return false;
    }

    const std::string meshPortable =
        AssetPathPolicy::ToPortableKey(request.projectRoot, meshAbs.generic_string());
    std::string controllerPortable;
    if (request.createController) {
        const std::string luaName = CharacterOwnedSource::ControllerScriptFileName(name);
        const std::filesystem::path luaAbs = ownedAbs / luaName;
        WriteControllerStub(luaAbs, name);
        controllerPortable =
            AssetPathPolicy::ToPortableKey(request.projectRoot, luaAbs.generic_string());
    }

    CharacterAsset asset = BuildAssetFromImport(name, meshPortable, inspect.imported,
                                                request.createController, controllerPortable);
    if (!CharacterSerializer::Serialize(asset, characterAbs, outError)) {
        std::filesystem::remove(characterAbs, ec);
        std::filesystem::remove_all(ownedAbs, ec);
        return false;
    }

    outPortableCharacterPath = portable;
    return true;
}

bool CharacterImporter::Reimport(const std::filesystem::path& projectRoot,
                                 const std::string& portableCharacterPath,
                                 const std::filesystem::path& candidateGlb,
                                 std::string& outError) {
    outError.clear();
    CharacterInspectResult inspect;
    if (!Inspect(candidateGlb, inspect, outError)) {
        return false;
    }

    const std::filesystem::path characterAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, portableCharacterPath);
    if (characterAbs.empty() || !std::filesystem::exists(characterAbs)) {
        outError = "Character asset not found: " + portableCharacterPath;
        return false;
    }

    CharacterAsset asset;
    if (!CharacterSerializer::Deserialize(asset, characterAbs, outError)) {
        return false;
    }

    const std::string meshKey =
        !asset.mesh.source.empty() ? asset.mesh.source : asset.skeleton.source;
    const std::filesystem::path ownedMeshAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, meshKey);

    std::error_code ec;
    const bool ownedExists = !ownedMeshAbs.empty() && std::filesystem::exists(ownedMeshAbs, ec);
    const bool candidateIsOwned =
        ownedExists && std::filesystem::equivalent(ownedMeshAbs, candidateGlb, ec);

    if (ownedExists && !candidateIsOwned) {
        SkeletalImportResult previous;
        std::string previousError;
        if (!SkeletalModelImporter::Load(ownedMeshAbs, {}, previous, previousError)) {
            outError = "Could not load the existing owned character mesh.\n" + previousError;
            return false;
        }
        const SkeletonCompatibilityReport report =
            SkeletonBuilder::Compare(previous.skeleton, inspect.imported.skeleton);
        const bool signatureOk = asset.skeleton.signature.empty() ||
                                 inspect.imported.skeleton.signature == asset.skeleton.signature;
        if (!report.compatible || !signatureOk) {
            outError = FormatCompatibilityFailure(report, asset.skeleton.signature,
                                                  inspect.imported.skeleton.signature, true);
            return false;
        }
    } else {
        if (!asset.skeleton.signature.empty() &&
            inspect.imported.skeleton.signature != asset.skeleton.signature) {
            SkeletonCompatibilityReport report;
            outError = FormatCompatibilityFailure(report, asset.skeleton.signature,
                                                  inspect.imported.skeleton.signature, false);
            return false;
        }
    }

    const std::string previousJson = CharacterSerializer::SerializeToString(asset);
    const bool preserve = asset.import.preserveAuthoredEditsOnReimport;
    asset.skeleton.skin = inspect.imported.skeleton.skinName;
    asset.skeleton.signature = inspect.imported.skeleton.signature;
    MergeSubmeshesFromImport(asset, inspect.imported);
    MergeClipsFromImport(asset, inspect.imported, meshKey, preserve);
    if (asset.defaults.animation.empty() || !preserve) {
        asset.defaults.animation = ChooseDefaultAnimation(asset.clips);
    } else {
        bool stillExists = false;
        for (const CharacterClipEntry& clip : asset.clips) {
            if (clip.id == asset.defaults.animation || clip.sourceClip == asset.defaults.animation) {
                stillExists = true;
                break;
            }
        }
        if (!stillExists) {
            asset.defaults.animation = ChooseDefaultAnimation(asset.clips);
        }
    }

    if (!AtomicWriteText(characterAbs, CharacterSerializer::SerializeToString(asset), outError)) {
        return false;
    }

    if (!candidateIsOwned) {
        if (ownedMeshAbs.empty()) {
            AtomicWriteText(characterAbs, previousJson, outError);
            outError = "Character has no owned mesh path to update.";
            return false;
        }
        const std::filesystem::path tmp =
            std::filesystem::path(ownedMeshAbs.native() + std::filesystem::path(".tmp").native());
        std::filesystem::copy_file(candidateGlb, tmp,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            AtomicWriteText(characterAbs, previousJson, outError);
            outError = "Failed to copy replacement GLB: " + ec.message();
            return false;
        }
        std::filesystem::rename(tmp, ownedMeshAbs, ec);
        if (ec) {
            std::filesystem::remove(ownedMeshAbs, ec);
            std::filesystem::rename(tmp, ownedMeshAbs, ec);
            if (ec) {
                std::filesystem::remove(tmp, ec);
                AtomicWriteText(characterAbs, previousJson, outError);
                outError = "Failed to replace owned mesh.glb: " + ec.message();
                return false;
            }
        }
    }

    CharacterRuntime::Invalidate(portableCharacterPath);
    return true;
}

} // namespace Hephaestus
