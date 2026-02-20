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

/**
 * @brief Callback type for loading a scene.
 * @param absolutePath Full path to scene file
 * @return true if scene loaded successfully
 */
using LoadSceneCallback = std::function<bool(const QString& absolutePath)>;

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

    /**
     * @brief Open an asset by its info.
     * @param absolutePath Full filesystem path
     * @param type Asset type
     * @return true if asset was handled
     */
    bool OpenByType(const QString& absolutePath, AssetType type);

    /**
     * @brief Set callback for scene loading.
     * Used to integrate with EditorContext::LoadScene.
     */
    void SetLoadSceneCallback(LoadSceneCallback callback);

private:
    bool openScene(const QString& absolutePath);
    bool openMaterial(const QString& absolutePath);
    bool openScript(const QString& absolutePath);
    bool openText(const QString& absolutePath);
    bool openExternal(const QString& absolutePath);
    void showNotImplemented(const QString& typeName, const QString& path);

    EditorMainWindow* m_mainWindow = nullptr;
    LoadSceneCallback m_loadSceneCallback;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
