#pragma once

#include "EditorDocument.h"

#include "../Assets/CharacterAsset.h"
#include "../Assets/CharacterRuntime.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

class EngineFrameRenderer;

enum class CharacterEditorItemKind {
    Character,
    Mesh,
    MeshPart,
    Skeleton,
    Bone,
    Materials,
    MaterialSlot,
    Animations,
    Folder,
    Clip,
    Sockets,
    Socket,
    Controller
};

struct CharacterEditorSelection {
    CharacterEditorItemKind kind = CharacterEditorItemKind::Character;
    std::string id;
    int index = -1;
};

class CharacterDocument : public EditorDocument {
public:
    explicit CharacterDocument(Scene* previewScene);

    EditorDocumentType GetType() const override { return EditorDocumentType::Character; }
    const std::string& GetDisplayName() const override { return displayName_; }
    Scene* GetScene() override { return scene_; }

    bool OnOpen(const std::filesystem::path& path, std::string& outError) override;
    bool OnSave(std::string& outError) override;

    void SetProjectRoot(std::filesystem::path root) { projectRoot_ = std::move(root); }
    const std::filesystem::path& GetProjectRoot() const { return projectRoot_; }
    const std::string& GetPortablePath() const { return portablePath_; }
    const std::string& LastError() const { return lastError_; }

    CharacterAsset& GetAsset() { return asset_; }
    const CharacterAsset& GetAsset() const { return asset_; }

    bool HasMeshSource() const;
    Entity FindPreviewCharacter() const;
    Entity FindSocketGizmo() const;

    void RebuildPreview();
    bool BindPreview(EngineFrameRenderer* renderer, bool applyAssetDefaults, bool forceGpuReload,
                     std::string& outError);
    void SyncPreviewClipMetadata();

    void BeginAuthoredEdit();
    void MarkAuthoredChanged();

    CharacterClipEntry* FindClipById(const std::string& clipId);
    const CharacterClipEntry* FindClipById(const std::string& clipId) const;
    CharacterSocket* FindSocketByName(const std::string& name);
    const CharacterSocket* FindSocketByName(const std::string& name) const;

    bool SetClipDisplayName(const std::string& clipId, const std::string& displayName, std::string& outError);
    bool SetClipFolder(const std::string& clipId, const std::string& folder, std::string& outError);
    bool SetClipLoop(const std::string& clipId, bool loop, std::string& outError);
    bool SetClipSpeed(const std::string& clipId, float speed, std::string& outError);
    bool SetClipTrim(const std::string& clipId, float startTime, float endTime, std::string& outError);
    bool DuplicateClip(const std::string& clipId, std::string& outNewId, std::string& outError);
    bool DeleteClip(const std::string& clipId, std::string& outError);

    bool AddAnimationFolder(const std::string& folder, std::string& outError);
    bool RenameAnimationFolder(const std::string& oldFolder, const std::string& newFolder, std::string& outError);
    bool DeleteEmptyAnimationFolder(const std::string& folder, std::string& outError);

    bool CreateSocket(const std::string& name, const std::string& bone, std::string& outError);
    bool DeleteSocket(const std::string& name, std::string& outError);
    bool RenameSocket(const std::string& oldName, const std::string& newName, std::string& outError);
    bool SetSocketBone(const std::string& name, const std::string& bone, std::string& outError);
    bool SetSocketTransform(const std::string& name, const float position[3], const float rotation[3],
                            std::string& outError);

    bool SetMaterialSlot(int slotIndex, const std::string& materialPath, std::string& outError);
    bool SetControllerEnabled(bool enabled);
    bool SetControllerScript(const std::string& scriptPath);
    bool SetDefaultAnimation(const std::string& clipId, std::string& outError);

    static std::string MakeUniqueClipId(const CharacterAsset& asset, const std::string& desired);

protected:
    std::string SerializeForUndo() override;
    bool DeserializeFromUndo(const std::string& data, std::string& outError) override;

private:
    void rebuildPreviewWorld();
    bool validateClipTrim(const CharacterClipEntry& clip, float startTime, float endTime,
                          std::string& outError) const;
    float sourceDurationForClip(const CharacterClipEntry& clip) const;

    Scene* scene_ = nullptr;
    CharacterAsset asset_;
    std::filesystem::path projectRoot_;
    std::string portablePath_;
    std::string lastError_;
};

} // namespace Hephaestus
