#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <functional>

#include "../Scene/Scene.h"
#include "../Scene/BlueprintAsset.h"

enum class EditorDocumentType {
    Scene,
    Blueprint
};

class EditorDocument {
public:
    virtual ~EditorDocument() = default;

    virtual EditorDocumentType GetType() const = 0;
    virtual const std::string& GetDisplayName() const = 0;
    virtual Scene* GetScene() = 0;

    virtual bool OnOpen(const std::filesystem::path& path, std::string& outError) = 0;
    virtual bool OnSave(std::string& outError) = 0;
    virtual void OnClose() {}
    virtual void OnFocus() {}
    virtual void OnTick(float /*dt*/) {}

    bool IsDirty() const { return dirty_; }
    void SetDirty(bool dirty) { dirty_ = dirty; }

    const std::filesystem::path& GetPath() const { return path_; }

    void PushUndoSnapshot();
    bool Undo(std::string& outError);
    void ClearUndo();

protected:
    virtual std::string SerializeForUndo() = 0;
    virtual bool DeserializeFromUndo(const std::string& data, std::string& outError) = 0;

    std::filesystem::path path_;
    std::string displayName_;
    bool dirty_ = false;
    std::vector<std::string> undoStack_;
};

class SceneDocument : public EditorDocument {
public:
    explicit SceneDocument(Scene* scene);

    EditorDocumentType GetType() const override { return EditorDocumentType::Scene; }
    const std::string& GetDisplayName() const override { return displayName_; }
    Scene* GetScene() override { return scene_; }

    bool OnOpen(const std::filesystem::path& path, std::string& outError) override;
    bool OnSave(std::string& outError) override;

    void SetProjectRoot(std::filesystem::path root) { projectRoot_ = std::move(root); }
    void SetPostLoadCallback(std::function<void(Scene&)> callback) { postLoadCallback_ = std::move(callback); }
    void SetPostUndoCallback(std::function<void(Scene&)> callback) { postUndoCallback_ = std::move(callback); }

protected:
    std::string SerializeForUndo() override;
    bool DeserializeFromUndo(const std::string& data, std::string& outError) override;

private:
    Scene* scene_ = nullptr;
    std::filesystem::path projectRoot_;
    std::function<void(Scene&)> postLoadCallback_;
    std::function<void(Scene&)> postUndoCallback_;
};

class BlueprintDocument : public EditorDocument {
public:
    explicit BlueprintDocument(Scene* previewScene);

    EditorDocumentType GetType() const override { return EditorDocumentType::Blueprint; }
    const std::string& GetDisplayName() const override { return displayName_; }
    Scene* GetScene() override { return scene_; }

    bool OnOpen(const std::filesystem::path& path, std::string& outError) override;
    bool OnSave(std::string& outError) override;
    void OnClose() override;

    BlueprintAsset& GetAsset() { return asset_; }
    const BlueprintAsset& GetAsset() const { return asset_; }

    bool SyncNodeFromEntity(Entity entity);
    bool AddChildNode(const std::string& parentId, const std::string& nameHint, Entity* outEntity = nullptr);
    bool DeleteNode(const std::string& nodeId);
    bool ReparentNode(const std::string& nodeId, const std::string& newParentId);

    bool IsRootNode(const std::string& nodeId) const { return nodeId == asset_.rootNodeId; }
    std::vector<std::string> GetNodeIds() const;

    void SetPostRebuildCallback(std::function<void(Scene&)> callback) { postRebuildCallback_ = std::move(callback); }

protected:
    std::string SerializeForUndo() override;
    bool DeserializeFromUndo(const std::string& data, std::string& outError) override;

private:
    void RebuildPreviewWorld();
    BlueprintNode* FindNodeById(const std::string& nodeId);
    Entity FindEntityByNodeId(const std::string& nodeId);
    std::string GenerateNodeId() const;

    Scene* scene_ = nullptr;
    BlueprintAsset asset_;
    entt::entity rootEntity_ = entt::null;
    std::string sceneSnapshotBeforeOpen_;
    std::function<void(Scene&)> postRebuildCallback_;
};
