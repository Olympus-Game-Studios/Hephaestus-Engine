/**
 * @file FbxSkeletalImporter.h
 * @brief ufbx scene → SkeletalImportResult / AnimationClip.
 */

#pragma once

#include "SkeletalModelImporter.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

class FbxSkeletalImporter {
public:
    static bool Load(const std::filesystem::path& path,
                     const SkeletalImportSettings& settings,
                     SkeletalImportResult& outResult,
                     std::string& outError);

    static bool LoadClips(const std::filesystem::path& path,
                          const SkeletalImportSettings& settings,
                          const Skeleton& character,
                          std::vector<AnimationClip>& outClips,
                          std::string& outError);
};

} // namespace Hephaestus
