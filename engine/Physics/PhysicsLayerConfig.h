#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Physics {

inline constexpr std::uint16_t kMaxGameplayLayers = 16;
inline constexpr std::uint16_t kLayerDefault = 0;
inline constexpr std::uint16_t kLayerWorld = 1;
inline constexpr std::uint16_t kLayerCharacter = 2;
inline constexpr std::uint16_t kLayerTrigger = 3;
inline constexpr std::uint16_t kInvalidLayer = 0xFFFF;

// Authoritative seed written for new projects. Omitted pairs in a present
// file still default to collide; this document lists every seed pair.
inline constexpr const char* kPhysicsLayerSeedJson = R"json({
  "version": 1,
  "layers": [
    { "id": 0, "name": "Default", "builtin": true },
    { "id": 1, "name": "World", "builtin": true },
    { "id": 2, "name": "Character", "builtin": true },
    { "id": 3, "name": "Trigger", "builtin": true }
  ],
  "tombstones": [],
  "matrix": [
    [0, 0, true],
    [0, 1, true],
    [0, 2, true],
    [0, 3, true],
    [1, 1, false],
    [1, 2, true],
    [1, 3, true],
    [2, 2, true],
    [2, 3, true],
    [3, 3, false]
  ]
})json";

struct PhysicsLayerDesc {
    std::uint16_t id = 0;
    std::string name;
    bool builtin = false;
};

struct PhysicsLayerLoadResult {
    bool missingFile = false;
    bool usedCompatibility = false;
    std::string error;
};

class PhysicsLayerConfig {
public:
    static PhysicsLayerConfig Compatibility();
    static PhysicsLayerConfig Seed();

    static PhysicsLayerLoadResult LoadFromFile(const std::filesystem::path& path, PhysicsLayerConfig& outConfig);
    static PhysicsLayerLoadResult LoadFromString(const std::string& json, PhysicsLayerConfig& outConfig);

    bool SaveToFile(const std::filesystem::path& path, std::string& outError) const;
    std::string ToJson() const;

    static std::filesystem::path ProjectFile(const std::filesystem::path& projectRoot);

    const std::vector<PhysicsLayerDesc>& Layers() const { return m_Layers; }
    const std::vector<std::uint16_t>& Tombstones() const { return m_Tombstones; }

    bool HasLayer(std::uint16_t id) const;
    bool IsTombstoned(std::uint16_t id) const;
    const PhysicsLayerDesc* FindById(std::uint16_t id) const;
    std::uint16_t FindIdByName(const std::string& name) const;

    bool Collides(std::uint16_t a, std::uint16_t b) const;
    void SetCollides(std::uint16_t a, std::uint16_t b, bool collides);

    // Unknown or tombstoned ids become 0. Logs one Physics warning per id per process.
    std::uint16_t Resolve(std::uint16_t id) const;

    bool AddUserLayer(const std::string& name, std::string& outError);
    bool RenameLayer(std::uint16_t id, const std::string& name, std::string& outError);
    bool DeleteUserLayer(std::uint16_t id, std::string& outError);

private:
    std::vector<PhysicsLayerDesc> m_Layers;
    std::vector<std::uint16_t> m_Tombstones;
    bool m_Matrix[kMaxGameplayLayers][kMaxGameplayLayers]{};

    void ClearMatrix();
    void SortLayers();
    bool NameTaken(const std::string& name, std::uint16_t exceptId) const;
};

} // namespace Physics
