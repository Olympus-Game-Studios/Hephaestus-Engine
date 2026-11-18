/**
 * @file CharacterRuntime.h
 * @brief Shared .hpcharacter load + CharacterComponent bind (editor and game).
 */

#pragma once

#include "CharacterAsset.h"
#include "SkeletalModelImporter.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Entity;
class Scene;

namespace Hephaestus {

class EngineFrameRenderer;

struct CharacterRuntimeData {
    CharacterAsset asset;
    SkeletalImportResult imported;
    std::vector<AnimationClip> clips;
    std::string portablePath;
    std::uint64_t fingerprint = 0;
    std::vector<std::string> warnings;
};

using CharacterClipSourceCache = std::unordered_map<std::string, std::vector<AnimationClip>>;

std::string ChooseDefaultAnimation(const std::vector<CharacterClipEntry>& clips);

class CharacterRuntime {
public:
    static bool Load(const std::filesystem::path& projectRoot,
                     const std::string& portableCharacterPath,
                     CharacterRuntimeData& outData,
                     std::string& outError);

    /// Resolve each authored clip entry against its own source file. Clips from the mesh
    /// source come from meshClips; other sources load on demand into extraSources.
    static std::vector<AnimationClip> BindAuthoredClips(const CharacterAsset& asset,
                                                        const std::filesystem::path& projectRoot,
                                                        const SkeletalImportSettings& settings,
                                                        const Skeleton& characterSkeleton,
                                                        const std::vector<AnimationClip>& meshClips,
                                                        CharacterClipSourceCache& extraSources,
                                                        std::vector<std::string>& warnings);

    static void Invalidate(const std::string& portableCharacterPath);
    static void InvalidateAll();
    static void InvalidateGpuMeshes(EngineFrameRenderer* renderer,
                                    const std::string& portableCharacterPath);

    /// Bind CPU playback state and stamp a runtime-generated MeshRenderer.
    /// Copies asset defaults onto the component only when applyAssetDefaults is true.
    static bool BindEntity(Entity entity,
                           const std::filesystem::path& projectRoot,
                           bool applyAssetDefaults,
                           std::string& outError);

    /// Upload skinned GPU mesh for an already-bound character. No-op if renderer is null.
    static bool UploadVisual(Entity entity,
                             const std::filesystem::path& projectRoot,
                             EngineFrameRenderer* renderer,
                             std::string& outError,
                             bool forceReload = false);

    /// Bind any CharacterComponent that is not runtime-ready, then upload visuals.
    /// Already-bound entities are not rebound so clip playback is preserved.
    static bool BindScene(Scene& scene,
                          const std::filesystem::path& projectRoot,
                          EngineFrameRenderer* renderer);

    static bool FindSocket(const std::string& portableCharacterPath,
                           const std::string& socketName,
                           CharacterSocket& outSocket,
                           std::string& outError);

    /// Calls fn with the skinned meshes of a loaded character; false when it is not loaded.
    static bool VisitSkinnedMeshes(const std::string& portableCharacterPath,
                                   const std::function<void(const std::vector<SkinnedMeshPart>&)>& fn);

private:
    static std::unordered_map<std::string, CharacterRuntimeData> s_cache;
};

} // namespace Hephaestus
