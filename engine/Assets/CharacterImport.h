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

    static CharacterAsset BuildAssetFromImport(const std::string& name,
                                               const std::string& meshPortable,
                                               const SkeletalImportResult& imported,
                                               bool createController,
                                               const std::string& controllerPortable);
};

} // namespace Hephaestus
