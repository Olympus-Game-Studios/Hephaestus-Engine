/**
 * @file EditorUndoCommand.cpp
 * @brief Implementation of snapshot-based undo for the Qt editor.
 *
 * The undo system works by serialising the entire scene to a JSON string
 * before each edit.  On undo the snapshot is deserialised back, restoring
 * the full scene state.  This matches the ImGui editor's approach exactly
 * (see EditorDocument::PushUndoSnapshot).
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorUndoCommand.h"
#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/SceneSerializer.h"
#include "../../Scene/PrefabOverrideSystem.h"

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

static constexpr size_t MAX_UNDO_SNAPSHOTS = 32;

struct UndoSnapshot {
    std::string label;
    std::string sceneData;
    std::string prefabPath;
    std::string prefabBackup;
};

static std::unordered_map<const Scene*, std::vector<UndoSnapshot>>& GetUndoStacks()
{
    static std::unordered_map<const Scene*, std::vector<UndoSnapshot>> s_stacks;
    return s_stacks;
}

static std::unordered_map<const Scene*, EditorUndoManager::CustomPushHandler>& GetCustomHandlers()
{
    static std::unordered_map<const Scene*, EditorUndoManager::CustomPushHandler> s_handlers;
    return s_handlers;
}

void EditorUndoManager::SetCustomPushHandler(Scene* scene, CustomPushHandler handler)
{
    if (!scene) {
        return;
    }
    GetCustomHandlers()[scene] = std::move(handler);
}

void EditorUndoManager::ClearCustomPushHandler(Scene* scene)
{
    if (!scene) {
        return;
    }
    GetCustomHandlers().erase(scene);
    GetUndoStacks().erase(scene);
}

void EditorUndoManager::PushSnapshot(EditorContext* ctx, const std::string& label)
{
    if (!ctx || !ctx->HasActiveScene()) {
        return;
    }
    Scene* scene = ctx->GetActiveScene();
    auto handlerIt = GetCustomHandlers().find(scene);
    if (handlerIt != GetCustomHandlers().end() && handlerIt->second) {
        handlerIt->second(label);
        return;
    }

    SceneSerializer serializer(*scene);
    std::string data = serializer.SerializeToString();
    if (data.empty()) {
        std::cerr << "[EditorUndoManager] Failed to serialize scene for undo snapshot\n";
        return;
    }

    auto& stack = GetUndoStacks()[scene];
    if (stack.size() >= MAX_UNDO_SNAPSHOTS) {
        stack.erase(stack.begin());
    }
    stack.push_back({ label, std::move(data), {}, {} });
}

void EditorUndoManager::PushSnapshotWithPrefabBackup(EditorContext* ctx, const std::string& label,
                                                    const std::string& prefabPath,
                                                    const std::string& prefabBackup)
{
    PushSnapshot(ctx, label);
    if (!ctx || !ctx->HasActiveScene()) {
        return;
    }
    auto& stack = GetUndoStacks()[ctx->GetActiveScene()];
    if (!stack.empty()) {
        stack.back().prefabPath = prefabPath;
        stack.back().prefabBackup = prefabBackup;
    }
}

bool EditorUndoManager::Undo(EditorContext* ctx, std::string& outError)
{
    if (!ctx || !ctx->HasActiveScene()) {
        outError = "No active scene";
        return false;
    }

    Scene* scene = ctx->GetActiveScene();
    auto handlerIt = GetCustomHandlers().find(scene);
    if (handlerIt != GetCustomHandlers().end()) {
        outError = "Use the Prefab Editor undo command.";
        return false;
    }

    auto& stack = GetUndoStacks()[scene];
    if (stack.empty()) {
        outError = "Nothing to undo";
        return false;
    }

    UndoSnapshot snap = std::move(stack.back());
    stack.pop_back();

    if (!snap.prefabPath.empty() && !snap.prefabBackup.empty()) {
        std::ofstream out(snap.prefabPath, std::ios::trunc);
        if (out.is_open()) {
            out << snap.prefabBackup;
            out.close();
            PrefabOverrideSystem::InvalidateAssetCache(snap.prefabPath);
        }
    }

    if (!SceneSerializer::DeserializeFromString(*scene, snap.sceneData)) {
        outError = "Failed to deserialize undo snapshot";
        return false;
    }

    ctx->ClearSelection();
    ctx->MarkDirty();
    return true;
}

bool EditorUndoManager::CanUndo(EditorContext* ctx)
{
    if (!ctx || !ctx->HasActiveScene()) {
        return false;
    }
    Scene* scene = ctx->GetActiveScene();
    if (GetCustomHandlers().count(scene) > 0) {
        return false;
    }
    auto it = GetUndoStacks().find(scene);
    return it != GetUndoStacks().end() && !it->second.empty();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
