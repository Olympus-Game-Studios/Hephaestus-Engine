/**
 * @file EditorUndoCommand.cpp
 * @brief Snapshot-based undo/redo for the Qt editor.
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

static std::unordered_map<const Scene*, std::vector<UndoSnapshot>>& GetRedoStacks()
{
    static std::unordered_map<const Scene*, std::vector<UndoSnapshot>> s_stacks;
    return s_stacks;
}

static std::unordered_map<const Scene*, EditorUndoManager::CustomPushHandler>& GetCustomHandlers()
{
    static std::unordered_map<const Scene*, EditorUndoManager::CustomPushHandler> s_handlers;
    return s_handlers;
}

static void TrimStack(std::vector<UndoSnapshot>& stack)
{
    while (stack.size() > MAX_UNDO_SNAPSHOTS) {
        stack.erase(stack.begin());
    }
}

static bool CaptureCurrentScene(Scene* scene, UndoSnapshot& outSnap, const std::string& label)
{
    SceneSerializer serializer(*scene);
    std::string data = serializer.SerializeToString();
    if (data.empty()) {
        return false;
    }
    outSnap.label = label;
    outSnap.sceneData = std::move(data);
    outSnap.prefabPath.clear();
    outSnap.prefabBackup.clear();
    return true;
}

static bool ApplySnapshot(EditorContext* ctx, Scene* scene, UndoSnapshot& snap, std::string& outError)
{
    if (!snap.prefabPath.empty() && !snap.prefabBackup.empty()) {
        std::ofstream out(snap.prefabPath, std::ios::trunc);
        if (out.is_open()) {
            out << snap.prefabBackup;
            out.close();
            PrefabOverrideSystem::InvalidateAssetCache(snap.prefabPath);
        }
    }

    if (!SceneSerializer::DeserializeFromString(*scene, snap.sceneData)) {
        outError = "Failed to deserialize undo/redo snapshot";
        return false;
    }

    ctx->ClearSelection();
    ctx->MarkDirty();
    return true;
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
    GetRedoStacks().erase(scene);
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

    UndoSnapshot snap;
    if (!CaptureCurrentScene(scene, snap, label)) {
        std::cerr << "[EditorUndoManager] Failed to serialize scene for undo snapshot\n";
        return;
    }

    auto& stack = GetUndoStacks()[scene];
    stack.push_back(std::move(snap));
    TrimStack(stack);
    GetRedoStacks()[scene].clear();
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

    UndoSnapshot current;
    if (!CaptureCurrentScene(scene, current, "Before undo")) {
        outError = "Failed to capture current scene for redo";
        return false;
    }

    UndoSnapshot snap = std::move(stack.back());
    stack.pop_back();

    if (!ApplySnapshot(ctx, scene, snap, outError)) {
        return false;
    }

    auto& redo = GetRedoStacks()[scene];
    redo.push_back(std::move(current));
    TrimStack(redo);
    return true;
}

bool EditorUndoManager::Redo(EditorContext* ctx, std::string& outError)
{
    if (!ctx || !ctx->HasActiveScene()) {
        outError = "No active scene";
        return false;
    }

    Scene* scene = ctx->GetActiveScene();
    if (GetCustomHandlers().count(scene) > 0) {
        outError = "Use the Prefab Editor redo command.";
        return false;
    }

    auto& redo = GetRedoStacks()[scene];
    if (redo.empty()) {
        outError = "Nothing to redo";
        return false;
    }

    UndoSnapshot current;
    if (!CaptureCurrentScene(scene, current, "Before redo")) {
        outError = "Failed to capture current scene for undo";
        return false;
    }

    UndoSnapshot snap = std::move(redo.back());
    redo.pop_back();

    if (!ApplySnapshot(ctx, scene, snap, outError)) {
        return false;
    }

    auto& stack = GetUndoStacks()[scene];
    stack.push_back(std::move(current));
    TrimStack(stack);
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

bool EditorUndoManager::CanRedo(EditorContext* ctx)
{
    if (!ctx || !ctx->HasActiveScene()) {
        return false;
    }
    Scene* scene = ctx->GetActiveScene();
    if (GetCustomHandlers().count(scene) > 0) {
        return false;
    }
    auto it = GetRedoStacks().find(scene);
    return it != GetRedoStacks().end() && !it->second.empty();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
