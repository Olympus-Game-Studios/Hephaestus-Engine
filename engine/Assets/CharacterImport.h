/**
 * @file CharacterImport.h
 * @brief Character inspect/import/reimport from GLB or FBX (shared by editor and tests).
 */

#pragma once

#include "CharacterAsset.h"
#include "SkeletalModelImporter.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

struct CharacterInspectResult {
    bool ok = false;
    bool canImport = false;
    std::string error;
    std::vector<std::string> warnings;
    std::string meshName;
    std::vector<std::string> skinnedMeshNames;
    std::string skinName;
    int boneCount = 0;
    int clipCount = 0;
    int materialSlotCount = 0;
    int skinnedMeshCount = 0;
    std::string sourceFormat;
    SkeletalImportResult imported;
};

struct CharacterImportRequest {
    std::filesystem::path sourceGlb;
    std::filesystem::path projectRoot;
    std::filesystem::path destinationFolderAbs;
    std::string characterName;
    bool createController = true;
    SkeletalImportSettings settings;
};

struct CharacterClipImportChoice {
    std::string sourceClip;
    std::string name; ///< Authored clip id and display name. Empty keeps the source take name.
};

class CharacterImporter {
public:
    static bool IsGlbPath(const std::filesystem::path& path);
    static bool IsFbxPath(const std::filesystem::path& path);
    static bool IsCharacterSourcePath(const std::filesystem::path& path);
    static SkeletalImportSettings MakeSettings(const CharacterImportSettings& import);

    /// True for Mixamo's motion take ("mixamo.com", optionally prefixed "<rig>|" or "<rig>/").
    static bool IsMixamoTake(const std::string& takeName);
    /// True for takes that carry no meaningful name: empty, "Take", "Take 001", or a Mixamo take.
    static bool IsGenericTakeName(const std::string& takeName);
    /// The clips worth importing from one source file. When it has a Mixamo take, the other
    /// generically named takes (Mixamo's empty "Take 001") are dropped.
    static std::vector<AnimationClip> ImportableClips(const std::vector<AnimationClip>& clips);

    static bool Inspect(const std::filesystem::path& sourceGlb,
                        CharacterInspectResult& outResult,
                        std::string& outError);
    static bool Inspect(const std::filesystem::path& sourceGlb,
                        const SkeletalImportSettings& settings,
                        CharacterInspectResult& outResult,
                        std::string& outError);

    static bool Import(const CharacterImportRequest& request,
                       std::string& outPortableCharacterPath,
                       std::string& outError);

    static bool Reimport(const std::filesystem::path& projectRoot,
                         const std::string& portableCharacterPath,
                         const std::filesystem::path& candidateGlb,
                         std::string& outError);

    /**
     * Assign the first owned mesh.glb / mesh.fbx to an empty .hpcharacter (New Character).
     * Preserves the existing controller script path.
     */
    static bool AssignCharacterSource(const std::filesystem::path& projectRoot,
                                      const std::string& portableCharacterPath,
                                      const std::filesystem::path& sourceGlb,
                                      std::string& outError);

    /**
     * Copy a GLB into the character owned-source folder and append selected clip
     * metadata to `asset`. Does not write the .hpcharacter file.
     * `choice.name` is the authored clip id and display name; empty keeps the source take name.
     */
    static bool ImportAnimationClips(const std::filesystem::path& projectRoot,
                                     CharacterAsset& asset,
                                     const std::string& portableCharacterPath,
                                     const std::filesystem::path& sourceGlb,
                                     const std::vector<CharacterClipImportChoice>& selectedClips,
                                     const std::string& destinationFolder,
                                     std::vector<std::string>& outAddedClipIds,
                                     std::string& outError);

    static CharacterAsset BuildAssetFromImport(const std::string& name,
                                               const std::string& meshPortable,
                                               const SkeletalImportResult& imported,
                                               bool createController,
                                               const std::string& controllerPortable);
};

} // namespace Hephaestus
