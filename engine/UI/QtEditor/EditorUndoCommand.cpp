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

#include <iostream>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Static undo stack (lives for the process lifetime, like the ImGui one)
// ---------------------------------------------------------------------------

static constexpr size_t MAX_UNDO_SNAPSHOTS = 32;

struct UndoSnapshot {
    std::string label;
    std::string sceneData;
};

static std::vector<UndoSnapshot>& GetUndoStack()
{
    static std::vector<UndoSnapshot> s_stack;
    return s_stack;
}

// ---------------------------------------------------------------------------
// PushSnapshot
// ---------------------------------------------------------------------------

void EditorUndoManager::PushSnapshot(EditorContext* ctx, const std::string& label)
{
    if (!ctx || !ctx->HasActiveScene()) return;

    SceneSerializer serializer(*ctx->GetActiveScene());
    std::string data = serializer.SerializeToString();
    if (data.empty()) {
        std::cerr << "[EditorUndoManager] Failed to serialize scene for undo snapshot\n";
        return;
    }

    auto& stack = GetUndoStack();
    if (stack.size() >= MAX_UNDO_SNAPSHOTS) {
        stack.erase(stack.begin()); // drop oldest
    }
    stack.push_back({ label, std::move(data) });
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

bool EditorUndoManager::Undo(EditorContext* ctx, std::string& outError)
{
    if (!ctx || !ctx->HasActiveScene()) {
        outError = "No active scene";
        return false;
    }

    auto& stack = GetUndoStack();
    if (stack.empty()) {
        outError = "Nothing to undo";
        return false;
    }

    UndoSnapshot snap = std::move(stack.back());
    stack.pop_back();

    Scene* scene = ctx->GetActiveScene();
    if (!SceneSerializer::DeserializeFromString(*scene, snap.sceneData)) {
        outError = "Failed to deserialize undo snapshot";
        return false;
    }

    // Selection may have been invalidated — clear it
    ctx->ClearSelection();
    ctx->MarkDirty();
    return true;
}

// ---------------------------------------------------------------------------
// CanUndo
// ---------------------------------------------------------------------------

bool EditorUndoManager::CanUndo(EditorContext* ctx)
{
    (void)ctx;
    return !GetUndoStack().empty();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
