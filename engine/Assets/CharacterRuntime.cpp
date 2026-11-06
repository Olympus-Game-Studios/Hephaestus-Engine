#include "CharacterRuntime.h"

#include "AssetPathPolicy.h"
#include "../Animation/CharacterPlayback.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Hephaestus {
namespace {

std::mutex g_cacheMutex;

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool EqualsInsensitive(const std::string& a, const std::string& b) {
    return ToLowerCopy(a) == ToLowerCopy(b);
}

bool EndsWithInsensitive(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    return EqualsInsensitive(value.substr(value.size() - suffix.size()), suffix);
}

std::uint64_t FileFingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return 0;
    }
    const auto size = std::filesystem::file_size(path, ec);
    const auto time = std::filesystem::last_write_time(path, ec);
    const auto ticks = static_cast<std::uint64_t>(time.time_since_epoch().count());
    return (static_cast<std::uint64_t>(size) * 1315423911ull) ^ ticks;
}

const AnimationClip* FindImportedClip(const std::vector<AnimationClip>& imported,
                                      const CharacterClipEntry& entry) {
    const std::string wanted = !entry.sourceClip.empty() ? entry.sourceClip : entry.id;
    for (const AnimationClip& clip : imported) {
        const std::string sourceName = clip.sourceClip.empty() ? clip.name : clip.sourceClip;
        if (sourceName == wanted || clip.name == wanted || clip.id == wanted) {
            return &clip;
        }
    }
    return nullptr;
}

void ApplyClipEntry(const CharacterClipEntry& entry, AnimationClip& clip) {
    if (!entry.id.empty()) {
        clip.id = entry.id;
    }
    clip.loop = entry.loop;
    clip.speed = entry.speed;
    clip.startTime = entry.startTime;
    clip.endTime = entry.endTime;
}

void ApplyMaterials(CharacterComponent& character,
                    const CharacterAsset& asset,
                    MeshRendererComponent& mesh) {
    const size_t slotCount = asset.mesh.submeshes.size();
    mesh.submeshCount = static_cast<int>(slotCount == 0 ? 1 : slotCount);
    mesh.appliedMaterialPaths.assign(static_cast<size_t>(mesh.submeshCount), "");
    mesh.materialIndices.assign(static_cast<size_t>(mesh.submeshCount), -1);
    for (size_t i = 0; i < slotCount; ++i) {
        std::string path = asset.mesh.submeshes[i].material;
        if (i < character.materialOverrides.size() && !character.materialOverrides[i].empty()) {
            path = character.materialOverrides[i];
        }
        mesh.appliedMaterialPaths[i] = path;
    }
    if (character.materialOverrides.size() < static_cast<size_t>(mesh.submeshCount)) {
        character.materialOverrides.resize(static_cast<size_t>(mesh.submeshCount));
    }
}

} // namespace

std::unordered_map<std::string, CharacterRuntimeData> CharacterRuntime::s_cache;

std::string ChooseDefaultAnimation(const std::vector<CharacterClipEntry>& clips) {
    for (const CharacterClipEntry& clip : clips) {
        if (EqualsInsensitive(clip.id, "Idle") || EqualsInsensitive(clip.sourceClip, "Idle") ||
            EqualsInsensitive(clip.displayName, "Idle") || EndsWithInsensitive(clip.id, "/Idle")) {
            return clip.id.empty() ? clip.sourceClip : clip.id;
        }
    }
    if (clips.size() == 1) {
        return clips.front().id.empty() ? clips.front().sourceClip : clips.front().id;
    }
    return {};
}

std::vector<AnimationClip> CharacterRuntime::BindAuthoredClips(
    const CharacterAsset& asset,
    const std::filesystem::path& projectRoot,
    const SkeletalImportSettings& settings,
    const Skeleton& characterSkeleton,
    const std::vector<AnimationClip>& meshClips,
    CharacterClipSourceCache& extraSources,
    std::vector<std::string>& warnings) {
    if (asset.clips.empty()) {
        return meshClips;
    }

    const std::string meshKey =
        !asset.mesh.source.empty() ? asset.mesh.source : asset.skeleton.source;
    std::vector<AnimationClip> clips;
    clips.reserve(asset.clips.size());
    for (const CharacterClipEntry& entry : asset.clips) {
        const std::string sourceKey = entry.source.empty() ? meshKey : entry.source;
        const std::vector<AnimationClip>* sourceClips = &meshClips;
        if (sourceKey != meshKey) {
            auto it = extraSources.find(sourceKey);
            if (it == extraSources.end()) {
                const std::filesystem::path extraAbs =
                    AssetPathPolicy::ResolvePortableKey(projectRoot, sourceKey);
                std::vector<AnimationClip> extra;
                std::string extraError;
                if (extraAbs.empty() || !std::filesystem::exists(extraAbs)) {
                    warnings.push_back("Missing extra animation source: " + sourceKey);
                } else if (!SkeletalModelImporter::LoadClips(extraAbs, settings, characterSkeleton,
                                                             extra, extraError)) {
                    warnings.push_back("Failed to load extra animation source '" + sourceKey +
                                       "': " + extraError);
                }
                it = extraSources.emplace(sourceKey, std::move(extra)).first;
            }
            sourceClips = &it->second;
        }
        const AnimationClip* found = FindImportedClip(*sourceClips, entry);
        if (!found) {
            warnings.push_back("Animation clip '" +
                               (entry.id.empty() ? entry.sourceClip : entry.id) +
                               "' was not found in " + sourceKey + ".");
            continue;
        }
        AnimationClip bound = *found;
        ApplyClipEntry(entry, bound);
        clips.push_back(std::move(bound));
    }
    return clips;
}

bool CharacterRuntime::Load(const std::filesystem::path& projectRoot,
                            const std::string& portableCharacterPath,
                            CharacterRuntimeData& outData,
                            std::string& outError) {
    outError.clear();
    if (portableCharacterPath.empty()) {
        outError = "Character path is empty.";
        return false;
    }

    const std::filesystem::path characterAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, portableCharacterPath);
    if (characterAbs.empty() || !std::filesystem::exists(characterAbs)) {
        outError = "Character asset not found: " + portableCharacterPath;
        return false;
    }

    const std::uint64_t characterStamp = FileFingerprint(characterAbs);
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = s_cache.find(portableCharacterPath);
        if (it != s_cache.end()) {
            const std::string meshKey = !it->second.asset.mesh.source.empty()
                                            ? it->second.asset.mesh.source
                                            : it->second.asset.skeleton.source;
            const std::filesystem::path meshAbs =
                AssetPathPolicy::ResolvePortableKey(projectRoot, meshKey);
            const std::uint64_t expected = characterStamp ^ FileFingerprint(meshAbs);
            if (it->second.fingerprint == expected) {
                outData = it->second;
                return true;
            }
        }
    }

    CharacterAsset asset;
    if (!CharacterSerializer::Deserialize(asset, characterAbs, outError)) {
        return false;
    }
    if (asset.mesh.source.empty() && asset.skeleton.source.empty()) {
        outError = "Character has no mesh source.";
        return false;
    }

    const std::string meshKey =
        !asset.mesh.source.empty() ? asset.mesh.source : asset.skeleton.source;
    const std::filesystem::path meshAbs =
        AssetPathPolicy::ResolvePortableKey(projectRoot, meshKey);
    if (meshAbs.empty() || !std::filesystem::exists(meshAbs)) {
        outError = "Character source mesh is missing: " + meshKey;
        return false;
    }

    SkeletalImportSettings settings;
    settings.unitScale = asset.import.unitScale;
    settings.animationSampleRate = asset.import.animationSampleRate;
    settings.stripBoneNamespace = asset.import.stripBoneNamespace;
    settings.fbxSpaceConversion = asset.import.fbxSpaceResolvedStored
                                     ? asset.import.fbxSpaceConversionResolved
                                     : asset.import.fbxSpaceConversion;
    SkeletalImportResult imported;
    if (!SkeletalModelImporter::Load(meshAbs, settings, imported, outError)) {
        return false;
    }

    if (!asset.skeleton.signature.empty() &&
        imported.skeleton.signature != asset.skeleton.signature) {
        outError =
            "Character source skeleton is no longer compatible with this character.\n\n"
            "Stored signature: " +
            asset.skeleton.signature + "\nSource signature: " + imported.skeleton.signature;
        return false;
    }

    std::vector<std::string> dataWarnings;
    CharacterClipSourceCache extraSources;
    std::vector<AnimationClip> clips =
        BindAuthoredClips(asset, projectRoot, settings, imported.skeleton, imported.clips,
                          extraSources, dataWarnings);

    CharacterRuntimeData data;
    data.asset = std::move(asset);
    data.imported = std::move(imported);
    data.clips = std::move(clips);
    data.portablePath = portableCharacterPath;
    data.fingerprint = characterStamp ^ FileFingerprint(meshAbs);
    data.warnings = std::move(dataWarnings);

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        s_cache[portableCharacterPath] = data;
    }
    outData = std::move(data);
    return true;
}

void CharacterRuntime::Invalidate(const std::string& portableCharacterPath) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    s_cache.erase(portableCharacterPath);
}

void CharacterRuntime::InvalidateAll() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    s_cache.clear();
}

bool CharacterRuntime::BindEntity(Entity entity,
                                  const std::filesystem::path& projectRoot,
                                  bool applyAssetDefaults,
                                  std::string& outError) {
    outError.clear();
    if (!entity || !entity.HasComponent<CharacterComponent>()) {
        outError = "Entity has no Character component.";
        return false;
    }

    auto& character = entity.GetComponent<CharacterComponent>();
    if (character.characterPath.empty()) {
        character.runtimeReady = false;
        character.lastError = "Character asset is not assigned.";
        outError = character.lastError;
        if (!entity.HasComponent<MeshRendererComponent>() ||
            IsRuntimeGeneratedVisual(entity.GetComponent<MeshRendererComponent>())) {
            auto& emptyMesh = entity.HasComponent<MeshRendererComponent>()
                                  ? entity.GetComponent<MeshRendererComponent>()
                                  : entity.AddComponent<MeshRendererComponent>();
            emptyMesh.runtimeGenerated = true;
            emptyMesh.meshAssetPath.clear();
        }
        return false;
    }

    CharacterRuntimeData data;
    if (!Load(projectRoot, character.characterPath, data, outError)) {
        character.runtimeReady = false;
        character.lastError = outError;
        if (!entity.HasComponent<MeshRendererComponent>() ||
            IsRuntimeGeneratedVisual(entity.GetComponent<MeshRendererComponent>())) {
            auto& failedMesh = entity.HasComponent<MeshRendererComponent>()
                                   ? entity.GetComponent<MeshRendererComponent>()
                                   : entity.AddComponent<MeshRendererComponent>();
            failedMesh.runtimeGenerated = true;
            failedMesh.meshAssetPath = character.characterPath;
        }
        return false;
    }

    if (applyAssetDefaults) {
        character.defaultAnimation = data.asset.defaults.animation;
        character.playbackSpeed = data.asset.defaults.playbackSpeed;
        character.runController = data.asset.controller.enabled;
        character.controllerScriptPath = data.asset.controller.scriptPath;
        character.materialOverrides.clear();
    }

    if (!CharacterAnimationSystem::BindRuntime(character, data.imported.skeleton, data.clips,
                                               outError)) {
        character.lastError = outError;
        return false;
    }

    auto& mesh = entity.HasComponent<MeshRendererComponent>()
                     ? entity.GetComponent<MeshRendererComponent>()
                     : entity.AddComponent<MeshRendererComponent>();
    mesh.runtimeGenerated = true;
    mesh.meshImported = true;
    mesh.meshAssetPath = character.characterPath;
    mesh.localBounds = data.imported.bounds;
    ApplyMaterials(character, data.asset, mesh);
    character.lastError.clear();
    return true;
}

bool CharacterRuntime::FindSocket(const std::string& portableCharacterPath,
                                  const std::string& socketName,
                                  CharacterSocket& outSocket,
                                  std::string& outError) {
    outError.clear();
    if (portableCharacterPath.empty()) {
        outError = "Character path is empty.";
        return false;
    }
    if (socketName.empty()) {
        outError = "Socket name is empty.";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = s_cache.find(portableCharacterPath);
    if (it == s_cache.end()) {
        outError = "Character runtime is not loaded.";
        return false;
    }
    for (const CharacterSocket& socket : it->second.asset.sockets) {
        if (socket.name == socketName) {
            outSocket = socket;
            return true;
        }
    }
    outError = "Socket '" + socketName + "' was not found.";
    return false;
}

} // namespace Hephaestus
