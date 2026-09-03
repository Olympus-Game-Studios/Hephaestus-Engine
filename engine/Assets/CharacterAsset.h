/**
 * @file CharacterAsset.h
 * @brief Authored .hpcharacter schema, serializer, and owned-source path helpers.
 *
 * The JSON file is metadata and portable references. Immutable skeleton,
 * mesh, and clip payloads live in owned source files under .source/.
 */

#pragma once

#include "FbxImportSettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

struct CharacterImportSettings {
    float unitScale = 1.0f;
    bool preserveAuthoredEditsOnReimport = true;
    FbxSpaceConversion fbxSpaceConversion = FbxSpaceConversion::Auto;
    FbxSpaceConversion fbxSpaceConversionResolved = FbxSpaceConversion::ModifyGeometry;
    bool fbxSpaceResolvedStored = false;
    float animationSampleRate = 0.0f;
    bool stripBoneNamespace = false;
};

struct CharacterSkeletonRef {
    std::string source;
    std::string skin;
    std::string signature;
};

struct CharacterSubmeshSlot {
    std::string name;
    std::string material;
    std::string meshPart; ///< Empty on legacy single-mesh assets.
};

inline std::string CharacterSubmeshIdentity(const CharacterSubmeshSlot& slot) {
    if (slot.meshPart.empty()) {
        return slot.name;
    }
    return slot.meshPart + "/" + slot.name;
}

struct CharacterMeshRef {
    std::string source;
    std::vector<CharacterSubmeshSlot> submeshes;
};

struct CharacterClipEntry {
    std::string id;
    std::string displayName;
    std::string folder;
    std::string source;
    std::string sourceClip;
    bool loop = true;
    float speed = 1.0f;
    float startTime = 0.0f;
    float endTime = 0.0f;
};

struct CharacterSocket {
    std::string name;
    std::string bone;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
};

struct CharacterControllerRef {
    std::string scriptPath;
    bool enabled = true;
};

struct CharacterDefaults {
    std::string animation;
    float playbackSpeed = 1.0f;
};

struct CharacterPhysicsStub {
    int assetVersion = 1;
};

struct CharacterAsset {
    static constexpr int kCurrentAssetVersion = 1;
    static constexpr const char* kSchema = "HephaestusCharacter";

    int assetVersion = kCurrentAssetVersion;
    std::string name;
    CharacterImportSettings import;
    CharacterSkeletonRef skeleton;
    CharacterMeshRef mesh;
    std::vector<std::string> animationFolders;
    std::vector<CharacterClipEntry> clips;
    std::vector<CharacterSocket> sockets;
    CharacterControllerRef controller;
    CharacterDefaults defaults;
    CharacterPhysicsStub physics;
    std::string sharedRig;
};

class CharacterOwnedSource {
public:
    /** Portable directory for owned sources, e.g. assets/characters/.source/Cultist */
    static std::string OwnedDirectoryPortableKey(const std::string& characterPortablePath);

    static std::string ControllerScriptFileName(const std::string& characterStem);

    static bool IsOwnedSourcePortableKey(const std::string& portablePath);

    static bool IsCharacterAssetPortableKey(const std::string& portablePath);

    static std::string CharacterStem(const std::string& characterPortablePath);

    /** Rewrite skeleton/mesh/clip/controller paths that live under oldOwnedDir. */
    static void RewriteOwnedDirectoryKeys(CharacterAsset& asset,
                                          const std::string& oldOwnedDir,
                                          const std::string& newOwnedDir);

    /**
     * After a .hpcharacter rename/move: relocate `.source/<Stem>/`, rewrite
     * portable keys inside the character JSON, and rename the controller Lua
     * when the stem changes.
     */
    static bool RelocateAfterCharacterMove(const std::filesystem::path& projectRoot,
                                           const std::string& oldCharacterPortable,
                                           const std::string& newCharacterPortable,
                                           std::string& outError);
};

class CharacterSerializer {
public:
    static CharacterAsset MakeEmpty(const std::string& name);

    static bool Deserialize(CharacterAsset& asset,
                            const std::filesystem::path& filepath,
                            std::string& outError);
    static bool DeserializeFromString(CharacterAsset& asset,
                                      const std::string& data,
                                      std::string& outError);
    static bool Serialize(const CharacterAsset& asset,
                          const std::filesystem::path& filepath,
                          std::string& outError);
    static std::string SerializeToString(const CharacterAsset& asset);

    /**
     * Write a valid empty .hpcharacter, create .source/<Stem>/, and a stub
     * controller Lua sidecar. Paths in the JSON are portable assets/ keys.
     */
    static bool CreateNew(const std::filesystem::path& characterAbsolutePath,
                          const std::filesystem::path& projectRoot,
                          std::string& outError);
};

} // namespace Hephaestus
