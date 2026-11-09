/**
 * @file CharacterImport.cpp
 * @brief Character inspect, import, and transactional reimport from GLB or FBX.
 */

#include "CharacterImport.h"

#include "AssetPathPolicy.h"
#include "CharacterRuntime.h"
#include "FbxDocument.h"

#include <nlohmann/json.hpp>
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../extern/glfw/deps/stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace Hephaestus {
namespace {

std::string SanitizeCharacterName(std::string name);

// Writes a texture beside the material and returns its portable path, or empty when there is none.
std::string WriteImportedTexture(const FbxTextureSource& texture, const std::filesystem::path& folder,
                                 const std::string& stem, const std::filesystem::path& projectRoot) {
    if (!texture.Exists()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path target;
    if (!texture.embedded.empty()) {
        target = folder / (stem + texture.extension);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(texture.embedded.data()),
                  static_cast<std::streamsize>(texture.embedded.size()));
        if (!out) {
            return {};
        }
    } else {
        target = folder / (stem + texture.file.extension().string());
        std::filesystem::copy_file(texture.file, target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return {};
        }
    }
    return AssetPathPolicy::ToPortableKey(projectRoot, target.generic_string());
}

// The glossiness map scales the Phong shininess exponent; each texel converts with the same formula ufbx and
// Blender use for the exponent itself (roughness = 1 - sqrt(exponent) / 10). Returns the PNG's portable path.
std::string WriteRoughnessFromGlossiness(const FbxTextureSource& gloss, float shininessExponent,
                                         const std::filesystem::path& folder, const std::string& stem,
                                         const std::filesystem::path& projectRoot) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = gloss.embedded.empty()
        ? stbi_load(gloss.file.string().c_str(), &width, &height, &channels, 1)
        : stbi_load_from_memory(gloss.embedded.data(), static_cast<int>(gloss.embedded.size()), &width, &height,
                                &channels, 1);
    if (!pixels) {
        return {};
    }
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < count; ++i) {
        const float exponent = std::max(0.0f, shininessExponent) * (static_cast<float>(pixels[i]) / 255.0f);
        const float roughness = std::clamp(1.0f - std::sqrt(exponent) / 10.0f, 0.0f, 1.0f);
        pixels[i] = static_cast<stbi_uc>(std::lround(roughness * 255.0f));
    }
    const std::filesystem::path target = folder / (stem + ".png");
    const bool written = stbi_write_png(target.string().c_str(), width, height, 1, pixels, width) != 0;
    stbi_image_free(pixels);
    return written ? AssetPathPolicy::ToPortableKey(projectRoot, target.generic_string()) : std::string();
}

// Gives each empty submesh slot a material built from the FBX material of the same name.
void ImportFbxSlotMaterials(const std::filesystem::path& projectRoot, const std::filesystem::path& sourceFbx,
                            const std::filesystem::path& ownedAbs, CharacterAsset& asset) {
    std::vector<FbxMaterialInfo> materials;
    std::string error;
    if (!ReadFbxMaterials(sourceFbx, materials, error)) {
        return;
    }
    const std::filesystem::path folder = ownedAbs / "materials";
    for (CharacterSubmeshSlot& slot : asset.mesh.submeshes) {
        if (!slot.material.empty()) {
            continue;
        }
        const auto found = std::find_if(materials.begin(), materials.end(),
                                        [&](const FbxMaterialInfo& m) { return m.name == slot.name; });
        if (found == materials.end()) {
            continue;
        }
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        const std::string stem = SanitizeCharacterName(slot.name);
        const std::string albedo = WriteImportedTexture(found->baseColorTexture, folder, stem + "_BaseColor",
                                                        projectRoot);
        const std::string normal = WriteImportedTexture(found->normalTexture, folder, stem + "_Normal", projectRoot);
        std::string roughnessMap = WriteImportedTexture(found->roughnessTexture, folder, stem + "_Roughness",
                                                        projectRoot);
        if (roughnessMap.empty() && found->glossinessTexture.Exists()) {
            roughnessMap = WriteRoughnessFromGlossiness(found->glossinessTexture, found->shininessExponent, folder,
                                                        stem + "_Roughness", projectRoot);
        }
        nlohmann::json material = {
            {"albedo", {found->baseColor.x, found->baseColor.y, found->baseColor.z}},
            {"metallic", found->metallic},
            {"roughness", roughnessMap.empty() ? found->roughness : 1.0f},
            {"ao", 1.0},
            {"useAlbedoTexture", !albedo.empty()},
            {"albedoTexturePath", albedo},
            {"useNormalTexture", !normal.empty()},
            {"normalTexturePath", normal},
            {"useRoughnessTexture", !roughnessMap.empty()},
            {"roughnessTexturePath", roughnessMap},
        };
        const std::filesystem::path materialAbs = folder / (stem + ".hematerial");
        std::ofstream out(materialAbs, std::ios::trunc);
        out << material.dump(2) << '\n';
        if (out) {
            slot.material = AssetPathPolicy::ToPortableKey(projectRoot, materialAbs.generic_string());
        }
    }
}

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

std::string MakeUniqueClipId(const CharacterAsset& asset, const std::string& desired);

void MergeClipsFromImport(CharacterAsset& asset,
                          const SkeletalImportResult& imported,
                          const std::string& meshPortable,
                          bool preserveAuthored) {
    std::unordered_map<std::string, CharacterClipEntry> previous;
    std::vector<CharacterClipEntry> otherSourceClips;
    for (const CharacterClipEntry& clip : asset.clips) {
        if (!clip.source.empty() && clip.source != meshPortable) {
            otherSourceClips.push_back(clip);
        } else if (preserveAuthored && !clip.sourceClip.empty()) {
            previous[clip.sourceClip] = clip;
        }
    }

    CharacterAsset taken;
    taken.clips = otherSourceClips;
    const std::vector<AnimationClip> importable = CharacterImporter::ImportableClips(imported.clips);
    std::vector<CharacterClipEntry> merged;
    merged.reserve(importable.size() + otherSourceClips.size());
    for (const AnimationClip& clip : importable) {
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
        if (it == previous.end()) {
            entry.id = MakeUniqueClipId(taken, entry.id);
        }
        if (entry.displayName.empty()) {
            entry.displayName = entry.sourceClip;
        }
        taken.clips.push_back(entry);
        merged.push_back(std::move(entry));
    }
    merged.insert(merged.end(), otherSourceClips.begin(), otherSourceClips.end());
    asset.clips = std::move(merged);
}

void MergeSubmeshesFromImport(CharacterAsset& asset, const SkeletalImportResult& imported) {
    std::unordered_map<std::string, std::string> materialsByIdentity;
    std::unordered_map<std::string, std::string> materialsByName;
    std::unordered_map<std::string, int> nameCount;
    for (const CharacterSubmeshSlot& slot : asset.mesh.submeshes) {
        if (!slot.name.empty()) {
            nameCount[slot.name] += 1;
            materialsByName[slot.name] = slot.material;
        }
        const std::string identity = CharacterSubmeshIdentity(slot);
        if (!identity.empty()) {
            materialsByIdentity[identity] = slot.material;
        }
    }
    asset.mesh.submeshes.clear();
    for (const SkinnedMeshPart& part : imported.meshes) {
        for (const SkinnedSubmeshData& src : part.submeshes) {
            CharacterSubmeshSlot slot;
            slot.meshPart = imported.meshes.size() > 1 ? part.name : std::string();
            slot.name = src.name.empty() ? ("Slot" + std::to_string(asset.mesh.submeshes.size()))
                                         : src.name;
            const std::string identity = CharacterSubmeshIdentity(slot);
            auto it = materialsByIdentity.find(identity);
            if (it != materialsByIdentity.end()) {
                slot.material = it->second;
            } else if (nameCount[slot.name] == 1) {
                const auto byName = materialsByName.find(slot.name);
                if (byName != materialsByName.end()) {
                    slot.material = byName->second;
                }
            }
            asset.mesh.submeshes.push_back(std::move(slot));
        }
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

std::string MakeUniqueClipId(const CharacterAsset& asset, const std::string& desired) {
    std::string base = desired.empty() ? "Clip" : desired;
    auto exists = [&](const std::string& id) {
        for (const CharacterClipEntry& clip : asset.clips) {
            if (clip.id == id) {
                return true;
            }
        }
        return false;
    };
    if (!exists(base)) {
        return base;
    }
    for (int i = 2; i < 10000; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (!exists(candidate)) {
            return candidate;
        }
    }
    return base + "_copy";
}

std::string OwnedMeshFileName(const std::filesystem::path& source) {
    const std::string ext = ToLowerCopy(source.extension().string());
    if (ext == ".fbx") {
        return "mesh.fbx";
    }
    return "mesh.glb";
}

std::string CharacterSourceFilterError() {
    return "Import Character requires a GLB or FBX file. Raw .gltf with external .bin / "
           "images is not supported; export a GLB from the DCC tool, or use FBX.";
}

void ApplyPersistedImportSettings(CharacterAsset& asset,
                                  const SkeletalImportSettings& requested,
                                  const SkeletalImportResult& imported) {
    asset.import.unitScale = requested.unitScale;
    asset.import.fbxSpaceConversion = requested.fbxSpaceConversion;
    asset.import.animationSampleRate = requested.animationSampleRate;
    asset.import.stripBoneNamespace = requested.stripBoneNamespace;
    if (imported.importedFromFbx) {
        asset.import.fbxSpaceConversionResolved = imported.fbxResolvedSpaceConversion;
        asset.import.fbxSpaceResolvedStored = true;
    }
}

void RewriteMeshSourceKeys(CharacterAsset& asset,
                           const std::string& oldKey,
                           const std::string& newKey) {
    if (oldKey.empty() || oldKey == newKey) {
        asset.skeleton.source = newKey;
        asset.mesh.source = newKey;
        return;
    }
    if (asset.skeleton.source == oldKey) {
        asset.skeleton.source = newKey;
    }
    if (asset.mesh.source == oldKey) {
        asset.mesh.source = newKey;
    }
    for (CharacterClipEntry& clip : asset.clips) {
        if (clip.source == oldKey) {
            clip.source = newKey;
        }
    }
}

} // namespace

bool CharacterImporter::IsMixamoTake(const std::string& takeName) {
    const std::string lower = ToLowerCopy(takeName);
    const std::string mixamo = "mixamo.com";
    if (lower == mixamo) {
        return true;
    }
    if (lower.size() <= mixamo.size() ||
        lower.compare(lower.size() - mixamo.size(), mixamo.size(), mixamo) != 0) {
        return false;
    }
    const char separator = lower[lower.size() - mixamo.size() - 1];
    return separator == '|' || separator == '/';
}

bool CharacterImporter::IsGenericTakeName(const std::string& takeName) {
    std::string compact;
    for (char c : ToLowerCopy(takeName)) {
        if (c != ' ' && c != '\t') {
            compact.push_back(c);
        }
    }
    if (compact.empty() || IsMixamoTake(takeName)) {
        return true;
    }
    if (compact.rfind("take", 0) != 0) {
        return false;
    }
    return std::all_of(compact.begin() + 4, compact.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::vector<AnimationClip> CharacterImporter::ImportableClips(const std::vector<AnimationClip>& clips) {
    auto takeName = [](const AnimationClip& clip) {
        return clip.sourceClip.empty() ? clip.name : clip.sourceClip;
    };
    const bool hasMixamoTake = std::any_of(clips.begin(), clips.end(), [&](const AnimationClip& clip) {
        return IsMixamoTake(takeName(clip));
    });
    if (!hasMixamoTake) {
        return clips;
    }
    std::vector<AnimationClip> importable;
    for (const AnimationClip& clip : clips) {
        const std::string name = takeName(clip);
        if (IsMixamoTake(name) || !IsGenericTakeName(name)) {
            importable.push_back(clip);
        }
    }
    return importable;
}

bool CharacterImporter::IsGlbPath(const std::filesystem::path& path) {
    return EqualsInsensitive(path.extension().string(), ".glb");
}

bool CharacterImporter::IsFbxPath(const std::filesystem::path& path) {
    return EqualsInsensitive(path.extension().string(), ".fbx");
}

bool CharacterImporter::IsCharacterSourcePath(const std::filesystem::path& path) {
    return IsGlbPath(path) || IsFbxPath(path);
}

SkeletalImportSettings CharacterImporter::MakeSettings(const CharacterImportSettings& import) {
    SkeletalImportSettings settings;
    settings.unitScale = import.unitScale;
    settings.animationSampleRate = import.animationSampleRate;
    settings.stripBoneNamespace = import.stripBoneNamespace;
    if (import.fbxSpaceResolvedStored) {
        settings.fbxSpaceConversion = import.fbxSpaceConversionResolved;
    } else {
        settings.fbxSpaceConversion = import.fbxSpaceConversion;
    }
    return settings;
}

bool CharacterImporter::Inspect(const std::filesystem::path& sourceGlb,
                                CharacterInspectResult& outResult,
                                std::string& outError) {
    return Inspect(sourceGlb, SkeletalImportSettings{}, outResult, outError);
}

bool CharacterImporter::Inspect(const std::filesystem::path& sourceGlb,
                                const SkeletalImportSettings& settings,
                                CharacterInspectResult& outResult,
                                std::string& outError) {
    outResult = {};
    outError.clear();
    if (!IsCharacterSourcePath(sourceGlb)) {
        outError = CharacterSourceFilterError();
        outResult.error = outError;
        return false;
    }
    if (!std::filesystem::exists(sourceGlb)) {
        outError = "Source file not found.";
        outResult.error = outError;
        return false;
    }

    if (!SkeletalModelImporter::Load(sourceGlb, settings, outResult.imported, outError)) {
        outResult.error = outError;
        return false;
    }

    outResult.ok = true;
    outResult.canImport = true;
    outResult.sourceFormat = IsFbxPath(sourceGlb) ? "FBX" : "GLB";
    outResult.skinName = outResult.imported.skeleton.skinName;
    outResult.boneCount = outResult.imported.skeleton.BoneCount();
    outResult.clipCount = static_cast<int>(outResult.imported.clips.size());
    outResult.skinnedMeshCount = static_cast<int>(outResult.imported.meshes.size());
    int slotCount = 0;
    for (const SkinnedMeshPart& part : outResult.imported.meshes) {
        outResult.skinnedMeshNames.push_back(part.name);
        slotCount += static_cast<int>(part.submeshes.size());
    }
    outResult.materialSlotCount = slotCount;
    if (!outResult.skinnedMeshNames.empty()) {
        outResult.meshName = outResult.skinnedMeshNames.front();
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
    if (!Inspect(request.sourceGlb, request.settings, inspect, outError)) {
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

    const std::filesystem::path meshAbs = ownedAbs / OwnedMeshFileName(request.sourceGlb);
    std::filesystem::copy_file(request.sourceGlb, meshAbs,
                               std::filesystem::copy_options::none, ec);
    if (ec) {
        std::filesystem::remove_all(ownedAbs, ec);
        outError = "Failed to copy character source: " + ec.message();
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
    ApplyPersistedImportSettings(asset, request.settings, inspect.imported);
    if (IsFbxPath(request.sourceGlb)) {
        ImportFbxSlotMaterials(request.projectRoot, request.sourceGlb, ownedAbs, asset);
    }
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

    const SkeletalImportSettings settings = MakeSettings(asset.import);
    CharacterInspectResult inspect;
    if (!Inspect(candidateGlb, settings, inspect, outError)) {
        return false;
    }

    const std::string meshKey =
        !asset.mesh.source.empty() ? asset.mesh.source : asset.skeleton.source;
    std::filesystem::path ownedMeshAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, meshKey);

    std::error_code ec;
    const bool ownedExists = !ownedMeshAbs.empty() && std::filesystem::exists(ownedMeshAbs, ec);
    const bool candidateIsOwned =
        ownedExists && std::filesystem::equivalent(ownedMeshAbs, candidateGlb, ec);

    if (ownedExists && !candidateIsOwned) {
        SkeletalImportResult previous;
        std::string previousError;
        if (!SkeletalModelImporter::Load(ownedMeshAbs, settings, previous, previousError)) {
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

    std::string destMeshKey = meshKey;
    std::filesystem::path destMeshAbs = ownedMeshAbs;
    if (!candidateIsOwned && !ownedMeshAbs.empty()) {
        const std::string newName = OwnedMeshFileName(candidateGlb);
        destMeshAbs = ownedMeshAbs.parent_path() / newName;
        destMeshKey = AssetPathPolicy::ToPortableKey(projectRoot, destMeshAbs.generic_string());
    }

    const std::string previousJson = CharacterSerializer::SerializeToString(asset);
    const bool preserve = asset.import.preserveAuthoredEditsOnReimport;
    RewriteMeshSourceKeys(asset, meshKey, destMeshKey);
    asset.skeleton.skin = inspect.imported.skeleton.skinName;
    asset.skeleton.signature = inspect.imported.skeleton.signature;
    if (inspect.imported.importedFromFbx) {
        asset.import.fbxSpaceConversionResolved = inspect.imported.fbxResolvedSpaceConversion;
        asset.import.fbxSpaceResolvedStored = true;
    }
    MergeSubmeshesFromImport(asset, inspect.imported);
    MergeClipsFromImport(asset, inspect.imported, destMeshKey, preserve);
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
        if (destMeshAbs.empty()) {
            AtomicWriteText(characterAbs, previousJson, outError);
            outError = "Character has no owned mesh path to update.";
            return false;
        }
        const std::filesystem::path tmp =
            std::filesystem::path(destMeshAbs.native() + std::filesystem::path(".tmp").native());
        std::filesystem::copy_file(candidateGlb, tmp,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            AtomicWriteText(characterAbs, previousJson, outError);
            outError = "Failed to copy replacement character source: " + ec.message();
            return false;
        }
        std::filesystem::rename(tmp, destMeshAbs, ec);
        if (ec) {
            std::filesystem::remove(destMeshAbs, ec);
            std::filesystem::rename(tmp, destMeshAbs, ec);
            if (ec) {
                std::filesystem::remove(tmp, ec);
                AtomicWriteText(characterAbs, previousJson, outError);
                outError = "Failed to replace owned character mesh: " + ec.message();
                return false;
            }
        }
        if (ownedExists && destMeshAbs != ownedMeshAbs) {
            std::filesystem::remove(ownedMeshAbs, ec);
        }
    }

    CharacterRuntime::Invalidate(portableCharacterPath);
    return true;
}

bool CharacterImporter::AssignCharacterSource(const std::filesystem::path& projectRoot,
                                              const std::string& portableCharacterPath,
                                              const std::filesystem::path& sourceGlb,
                                              std::string& outError) {
    outError.clear();
    CharacterAsset asset;
    const std::filesystem::path characterAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, portableCharacterPath);
    if (characterAbs.empty() || !std::filesystem::exists(characterAbs)) {
        outError = "Character asset not found: " + portableCharacterPath;
        return false;
    }
    if (!CharacterSerializer::Deserialize(asset, characterAbs, outError)) {
        return false;
    }

    const std::string meshKey =
        !asset.mesh.source.empty() ? asset.mesh.source : asset.skeleton.source;
    const std::filesystem::path ownedMeshAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, meshKey);
    std::error_code ec;
    const bool ownedExists = !ownedMeshAbs.empty() && std::filesystem::exists(ownedMeshAbs, ec);
    if (ownedExists) {
        return Reimport(projectRoot, portableCharacterPath, sourceGlb, outError);
    }

    CharacterInspectResult inspect;
    if (!Inspect(sourceGlb, MakeSettings(asset.import), inspect, outError)) {
        return false;
    }

    const std::string ownedDir = CharacterOwnedSource::OwnedDirectoryPortableKey(portableCharacterPath);
    const std::filesystem::path ownedAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, ownedDir);
    if (ownedAbs.empty()) {
        outError = "Failed to resolve owned source directory.";
        return false;
    }
    std::filesystem::create_directories(ownedAbs, ec);
    if (ec) {
        outError = "Failed to create owned source directory: " + ec.message();
        return false;
    }

    const std::filesystem::path meshAbs = ownedAbs / OwnedMeshFileName(sourceGlb);
    std::filesystem::copy_file(sourceGlb, meshAbs, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Failed to copy character source: " + ec.message();
        return false;
    }

    const std::string meshPortable =
        AssetPathPolicy::ToPortableKey(projectRoot, meshAbs.generic_string());
    asset.skeleton.source = meshPortable;
    asset.skeleton.skin = inspect.imported.skeleton.skinName;
    asset.skeleton.signature = inspect.imported.skeleton.signature;
    asset.mesh.source = meshPortable;
    MergeSubmeshesFromImport(asset, inspect.imported);
    MergeClipsFromImport(asset, inspect.imported, meshPortable, false);
    ApplyPersistedImportSettings(asset, MakeSettings(asset.import), inspect.imported);
    asset.defaults.animation = ChooseDefaultAnimation(asset.clips);
    if (asset.defaults.playbackSpeed <= 0.0f) {
        asset.defaults.playbackSpeed = 1.0f;
    }

    if (!AtomicWriteText(characterAbs, CharacterSerializer::SerializeToString(asset), outError)) {
        return false;
    }
    CharacterRuntime::Invalidate(portableCharacterPath);
    return true;
}

bool CharacterImporter::ImportAnimationClips(const std::filesystem::path& projectRoot,
                                             CharacterAsset& asset,
                                             const std::string& portableCharacterPath,
                                             const std::filesystem::path& sourceGlb,
                                             const std::vector<CharacterClipImportChoice>& selectedClips,
                                             const std::string& destinationFolder,
                                             std::vector<std::string>& outAddedClipIds,
                                             std::string& outError) {
    outError.clear();
    outAddedClipIds.clear();

    if (!IsCharacterSourcePath(sourceGlb)) {
        outError =
            "Import Animation requires a GLB or FBX file. Raw .gltf with external .bin / "
            "images is not supported.";
        return false;
    }

    const std::string meshKey =
        !asset.mesh.source.empty() ? asset.mesh.source : asset.skeleton.source;
    if (meshKey.empty()) {
        outError = "Assign a character mesh before importing animation clips.";
        return false;
    }
    const std::filesystem::path meshAbs = AssetPathPolicy::ResolvePortableKey(projectRoot, meshKey);
    if (meshAbs.empty() || !std::filesystem::exists(meshAbs)) {
        outError = "Character source mesh is missing: " + meshKey;
        return false;
    }

    const SkeletalImportSettings settings = MakeSettings(asset.import);
    SkeletalImportResult characterImport;
    if (!SkeletalModelImporter::Load(meshAbs, settings, characterImport, outError)) {
        return false;
    }

    std::vector<AnimationClip> importedClips;
    if (!SkeletalModelImporter::LoadClips(sourceGlb, settings, characterImport.skeleton, importedClips,
                                          outError)) {
        outError = "Animation cannot be imported.\n\nSkeleton incompatibility detected.\n" + outError;
        return false;
    }
    if (importedClips.empty()) {
        outError = "No animation clips were found in the source file.";
        return false;
    }

    std::vector<AnimationClip> chosen;
    std::vector<std::string> chosenNames;
    if (selectedClips.empty()) {
        outError = "No animation clips were selected.";
        return false;
    }
    for (const CharacterClipImportChoice& choice : selectedClips) {
        const std::string& wanted = choice.sourceClip;
        const AnimationClip* found = nullptr;
        for (const AnimationClip& clip : importedClips) {
            const std::string sourceName = clip.sourceClip.empty() ? clip.name : clip.sourceClip;
            if (sourceName == wanted || clip.name == wanted || clip.id == wanted) {
                found = &clip;
                break;
            }
        }
        if (!found) {
            outError = "Selected clip '" + wanted + "' was not found in the source file.";
            return false;
        }
        chosen.push_back(*found);
        chosenNames.push_back(choice.name);
    }
    if (chosen.empty()) {
        outError = "No animation clips were selected.";
        return false;
    }

    const std::string ownedDir = CharacterOwnedSource::OwnedDirectoryPortableKey(portableCharacterPath);
    const std::filesystem::path ownedAbs = AssetPathPolicy::ResolvePortableKey(projectRoot, ownedDir);
    if (ownedAbs.empty()) {
        outError = "Failed to resolve owned source directory.";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(ownedAbs, ec);

    std::string stem = sourceGlb.stem().string();
    if (stem.empty()) {
        stem = "anim";
    }
    std::string ext = ToLowerCopy(sourceGlb.extension().string());
    if (ext.empty()) {
        ext = ".glb";
    }
    std::filesystem::path destAbs = ownedAbs / (stem + ext);
    int suffix = 2;
    while (std::filesystem::exists(destAbs, ec)) {
        destAbs = ownedAbs / (stem + "_" + std::to_string(suffix) + ext);
        ++suffix;
        if (suffix > 10000) {
            outError = "Failed to allocate a unique owned animation filename.";
            return false;
        }
    }
    std::filesystem::copy_file(sourceGlb, destAbs, std::filesystem::copy_options::none, ec);
    if (ec) {
        outError = "Failed to copy animation source: " + ec.message();
        return false;
    }

    const std::string clipSourcePortable =
        AssetPathPolicy::ToPortableKey(projectRoot, destAbs.generic_string());
    if (!destinationFolder.empty()) {
        bool folderKnown = false;
        for (const std::string& folder : asset.animationFolders) {
            if (folder == destinationFolder) {
                folderKnown = true;
                break;
            }
        }
        if (!folderKnown) {
            asset.animationFolders.push_back(destinationFolder);
        }
    }

    for (size_t i = 0; i < chosen.size(); ++i) {
        const AnimationClip& clip = chosen[i];
        CharacterClipEntry entry;
        const std::string sourceName = clip.sourceClip.empty() ? clip.name : clip.sourceClip;
        const std::string desiredName = chosenNames[i].empty()
            ? (clip.id.empty() ? sourceName : clip.id)
            : chosenNames[i];
        entry.id = MakeUniqueClipId(asset, desiredName);
        entry.displayName = chosenNames[i].empty()
            ? (clip.name.empty() ? sourceName : clip.name)
            : chosenNames[i];
        entry.folder = destinationFolder;
        entry.source = clipSourcePortable;
        entry.sourceClip = sourceName;
        entry.loop = true;
        entry.speed = 1.0f;
        entry.startTime = 0.0f;
        entry.endTime = clip.endTime > 0.0f ? clip.endTime : clip.duration;
        outAddedClipIds.push_back(entry.id);
        asset.clips.push_back(std::move(entry));
    }
    return true;
}

} // namespace Hephaestus
