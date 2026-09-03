/**
 * @file SkeletalModelImporter.h
 * @brief glTF/GLB skin import into a Hephaestus skeleton (no static vertex baking).
 */

#pragma once

#include "FbxImportSettings.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

struct SkinnedVertexData {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 1.0f, 0.0f};
    float color[3] = {1.0f, 1.0f, 1.0f};
    float texCoord[2] = {0.0f, 0.0f};
    uint16_t joints[4] = {0, 0, 0, 0}; ///< GPU palette / eval indices
    float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

struct SkinnedSubmeshData {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int materialSlot = 0;
    std::string name;
};

struct SkeletalImportSettings {
    float unitScale = 1.0f;
    FbxSpaceConversion fbxSpaceConversion = FbxSpaceConversion::Auto;
    float animationSampleRate = 0.0f;
    bool stripBoneNamespace = false;
};

struct SkeletalImportResult {
    Skeleton skeleton;
    std::vector<SkinnedVertexData> vertices;
    std::vector<uint32_t> indices;
    std::vector<SkinnedSubmeshData> submeshes;
    AABB bounds{};
    Mat4 meshNodeWorld = Mat4::Identity();
    std::vector<std::string> warnings;
    std::vector<AnimationClip> clips;
    bool clampedInfluences = false;
    bool restBindMismatch = false;
    bool importedFromFbx = false;
    FbxSpaceConversion fbxResolvedSpaceConversion = FbxSpaceConversion::ModifyGeometry;
};

class SkeletalModelImporter {
public:
    static bool Load(const std::filesystem::path& path,
                     const SkeletalImportSettings& settings,
                     SkeletalImportResult& outResult,
                     std::string& outError);

    /// Import animation clips from a GLB and bind them to `character` by bone name.
    /// Files with a skin must be skeleton-compatible; unnamed/duplicate joints fail.
    static bool LoadClips(const std::filesystem::path& path,
                          const SkeletalImportSettings& settings,
                          const Skeleton& character,
                          std::vector<AnimationClip>& outClips,
                          std::string& outError);

    /// Two-bone rest-pose dummy used to exercise GPU skinning in editor and game.
    static void BuildRestPoseFixture(SkeletalImportResult& outResult,
                                     std::vector<Mat4>& outPalette);
};

} // namespace Hephaestus
