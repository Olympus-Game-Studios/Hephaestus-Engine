#pragma once

/**
 * @file EditorUndoCommand.h
 * @brief Lightweight undo/redo system for the Qt editor.
 *
 * Uses a QUndoStack-compatible approach built on top of the existing
 * EditorDocument snapshot system.  Each undoable action creates a command
 * that captures a *before* snapshot and (if needed) an *after* snapshot.
 *
 * Two flavours:
 *   1. Snapshot-based undo — serialises the whole scene before the action;
 *      undo restores that snapshot.  Simple and matches the ImGui behaviour.
 *   2. Fine-grained undo — captures a lambda pair (do/undo) for individual
 *      property changes.  More efficient; optional.
 *
 * The Qt Details panel calls PushUndoSnapshot() on the active EditorDocument
 * before mutating ECS state, keeping the behavior identical to the ImGui path.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QString>
#include <functional>
#include <string>
#include <vector>

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

// ---------------------------------------------------------------------------
// EditorUndoManager — thin wrapper to push snapshot undo through EditorDocument
// ---------------------------------------------------------------------------

/**
 * @brief Manages undo for the Qt editor by delegating to the existing
 *        EditorDocument::PushUndoSnapshot() / Undo() mechanism.
 *
 * Usage from any inspector:
 *   EditorUndoManager::PushSnapshot(m_ctx, "Change light color");
 *   // ... modify component ...
 *   m_ctx->MarkDirty();
 */
class EditorUndoManager
{
public:
    /**
     * Push a full-scene undo snapshot before an edit.
     * @param ctx     Editor context (used to reach the active document).
     * @param label   Human-readable description (for future redo UI).
     */
    static void PushSnapshot(EditorContext* ctx, const std::string& label = "");

    /**
     * Execute undo — restores the last snapshot.
     * @param ctx     Editor context.
     * @param outError  Receives an error message on failure.
     * @return true on success.
     */
    static bool Undo(EditorContext* ctx, std::string& outError);

    /**
     * Returns true if there is at least one undo snapshot available.
     */
    static bool CanUndo(EditorContext* ctx);
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
