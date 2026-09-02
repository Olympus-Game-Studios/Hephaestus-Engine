#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Hephaestus {

struct EditorCameraBookmark {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float orthoHalfExtent = 10.0f;
    bool orthographic = false;
};

struct EditorViewState {
    std::unordered_set<uint32_t> hiddenEntities;
    std::unordered_set<uint32_t> lockedEntities;
    std::unordered_map<int, EditorCameraBookmark> bookmarks;

    static std::filesystem::path StatePath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneKey);
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
};

} // namespace Hephaestus
