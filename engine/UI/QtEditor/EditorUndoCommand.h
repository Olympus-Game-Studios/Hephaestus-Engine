#pragma once

/**
 * @file EditorUndoCommand.h
 * @brief Lightweight undo/redo system for the Qt editor.
 *
 * Snapshot-based undo/redo serialises the whole scene before each action.
 * Redo is cleared whenever a new snapshot is pushed after undoing.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QString>
#include <functional>
#include <string>
#include <vector>

class Scene;

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

class EditorUndoManager
{
public:
    /**
     * Push a full-scene undo snapshot before an edit.
     * Clears the redo stack for the active scene.
     */
    static void PushSnapshot(EditorContext* ctx, const std::string& label = "");

    /**
     * Push a scene snapshot and keep a backup of a prefab file so Apply-to-Prefab
     * can restore both the scene and the source asset on undo.
     */
    static void PushSnapshotWithPrefabBackup(EditorContext* ctx, const std::string& label,
                                             const std::string& prefabPath,
                                             const std::string& prefabBackup);

    /**
     * Undo — restores the last snapshot and pushes the current state onto redo.
     */
    static bool Undo(EditorContext* ctx, std::string& outError);

    /**
     * Redo — restores the last redo snapshot and pushes the current state onto undo.
     */
    static bool Redo(EditorContext* ctx, std::string& outError);

    static bool CanUndo(EditorContext* ctx);
    static bool CanRedo(EditorContext* ctx);

    using CustomPushHandler = std::function<void(const std::string& label)>;
    static void SetCustomPushHandler(Scene* scene, CustomPushHandler handler);
    static void ClearCustomPushHandler(Scene* scene);
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
