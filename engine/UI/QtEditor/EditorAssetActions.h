/**
 * @file EditorAssetActions.h
 * @brief Asset action handlers for the Qt editor.
 *
 * Handles opening different asset types:
 * - Scenes: Load into the editor
 * - Scripts: Open in external editor
 * - Text: Open in dialog or external
 * - Others: Show info or "not implemented"
 */

#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QString>
#include <functional>
#include "../../Assets/AssetTypes.h"

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

class EditorMainWindow;
class PrefabEditorManager;

using LoadSceneCallback = std::function<bool(const QString& absolutePath)>;
using OpenPrefabCallback = std::function<bool(const QString& absolutePath)>;

/**
 * @brief Central handler for opening assets from the browser.
 *
 * This class determines what action to take based on asset type
 * and dispatches to the appropriate handler.
 */
class EditorAssetActions {
public:
    explicit EditorAssetActions(EditorMainWindow* mainWindow);
    ~EditorAssetActions() = default;

    /**
     * @brief Open an asset by its relative path.
     * @param relativePath Path relative to asset root
     * @return true if asset was handled (may still fail)
     */
    bool Open(const QString& relativePath);
    bool OpenInIDE(const QString& relativePath);

    /**
     * @brief Open an asset by its info.
     * @param absolutePath Full filesystem path
     * @param type Asset type
     * @return true if asset was handled
     */
    bool OpenByType(const QString& absolutePath, AssetType type);

    void SetLoadSceneCallback(LoadSceneCallback callback);
    void SetOpenPrefabCallback(OpenPrefabCallback callback);

private:
    bool openScene(const QString& absolutePath);
    bool openPrefab(const QString& absolutePath);
    bool openCharacter(const QString& absolutePath);
    bool openMaterial(const QString& absolutePath);
    bool openHUI(const QString& absolutePath);
    bool openScript(const QString& absolutePath);
    bool openText(const QString& absolutePath);
    bool openInIDEAbsolute(const QString& absolutePath);
    bool openExternal(const QString& absolutePath);
    void showNotImplemented(const QString& typeName, const QString& path);

    EditorMainWindow* m_mainWindow = nullptr;
    LoadSceneCallback m_loadSceneCallback;
    OpenPrefabCallback m_openPrefabCallback;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
