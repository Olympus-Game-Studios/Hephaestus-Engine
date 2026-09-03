/**
 * @file CharacterImport.h
 * @brief GLB character inspect/import/reimport (shared by editor and tests).
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
    std::string skinName;
    int boneCount = 0;
    int clipCount = 0;
    int materialSlotCount = 0;
    SkeletalImportResult imported;
};

struct CharacterImportRequest {
    std::filesystem::path sourceGlb;
    std::filesystem::path projectRoot;
    std::filesystem::path destinationFolderAbs;
    std::string characterName;
    bool createController = true;
};

class CharacterImporter {
public:
    static bool IsGlbPath(const std::filesystem::path& path);

    static bool Inspect(const std::filesystem::path& sourceGlb,
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
     * Assign the first owned mesh.glb to an empty .hpcharacter (New Character).
     * Preserves the existing controller script path.
     */
    static bool AssignCharacterSource(const std::filesystem::path& projectRoot,
                                      const std::string& portableCharacterPath,
                                      const std::filesystem::path& sourceGlb,
                                      std::string& outError);

    /**
     * Copy a GLB into the character owned-source folder and append selected clip
     * metadata to `asset`. Does not write the .hpcharacter file.
     */
    static bool ImportAnimationClips(const std::filesystem::path& projectRoot,
                                     CharacterAsset& asset,
                                     const std::string& portableCharacterPath,
                                     const std::filesystem::path& sourceGlb,
                                     const std::vector<std::string>& selectedSourceClips,
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
