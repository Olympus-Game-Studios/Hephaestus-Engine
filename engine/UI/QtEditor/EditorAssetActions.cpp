/**
 * @file EditorAssetActions.cpp
 * @brief Implementation of asset action handlers.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorAssetActions.h"
#include "EditorMainWindow.h"
#include "HUIVisualEditorDialog.h"
#include "MaterialGraphQtEditorDialog.h"
#include "TextureViewerDialog.h"
#include "../../Assets/AssetManager.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

EditorAssetActions::EditorAssetActions(EditorMainWindow* mainWindow)
    : m_mainWindow(mainWindow)
{
}

bool EditorAssetActions::Open(const QString& relativePath)
{
    auto& assetMgr = AssetManager::GetInstance();
    
    auto assetInfo = assetMgr.GetAssetInfo(relativePath.toStdString());
    if (!assetInfo) {
        std::cerr << "[EditorAssetActions] Asset not found: " << relativePath.toStdString() << "\n";
        return false;
    }

    QString absolutePath = QString::fromStdString(assetInfo->absolutePath);
    return OpenByType(absolutePath, assetInfo->type);
}

bool EditorAssetActions::OpenInIDE(const QString& relativePath)
{
    auto& assetMgr = AssetManager::GetInstance();

    auto assetInfo = assetMgr.GetAssetInfo(relativePath.toStdString());
    if (!assetInfo) {
        std::cerr << "[EditorAssetActions] Asset not found: " << relativePath.toStdString() << "\n";
        return false;
    }

    return openInIDEAbsolute(QString::fromStdString(assetInfo->absolutePath));
}

bool EditorAssetActions::OpenByType(const QString& absolutePath, AssetType type)
{
    std::cout << "[EditorAssetActions] Opening " << ToString(type) 
              << ": " << absolutePath.toStdString() << "\n";

    switch (type) {
        case AssetType::Scene:
            return openScene(absolutePath);

        case AssetType::Script:
            return openScript(absolutePath);

        case AssetType::Text:
            return openText(absolutePath);

        case AssetType::Texture:
            TextureViewerDialog::Open(m_mainWindow, absolutePath);
            return true;

        case AssetType::StaticMesh:
            // Mesh assets are placed into scenes via drag/drop in the editor.
            return false;

        case AssetType::Material:
            return openMaterial(absolutePath);

        case AssetType::Audio:
            // Could open audio player in the future
            return openExternal(absolutePath);

        case AssetType::Prefab:
            return openPrefab(absolutePath);

        case AssetType::Character:
            return openCharacter(absolutePath);

        case AssetType::UILayout:
            return openHUI(absolutePath);

        case AssetType::Shader:
            return openScript(absolutePath);

        case AssetType::Font:
            return openExternal(absolutePath);

        case AssetType::Folder:
            // Should not happen - folders are navigated, not opened
            return false;

        default:
            // Try to open externally for unknown types
            return openExternal(absolutePath);
    }
}

void EditorAssetActions::SetLoadSceneCallback(LoadSceneCallback callback)
{
    m_loadSceneCallback = std::move(callback);
}

void EditorAssetActions::SetOpenPrefabCallback(OpenPrefabCallback callback)
{
    m_openPrefabCallback = std::move(callback);
}

void EditorAssetActions::SetOpenCharacterCallback(OpenCharacterCallback callback)
{
    m_openCharacterCallback = std::move(callback);
}

void EditorAssetActions::SetOpenScriptCallback(OpenScriptCallback callback)
{
    m_openScriptCallback = std::move(callback);
}

bool EditorAssetActions::openScene(const QString& absolutePath)
{
    if (m_loadSceneCallback) {
        return m_loadSceneCallback(absolutePath);
    }

    // Fallback error
    QMessageBox::warning(m_mainWindow, 
        QObject::tr("Cannot Open Scene"),
        QObject::tr("Scene loading callback not configured."));
    return false;
}

bool EditorAssetActions::openPrefab(const QString& absolutePath)
{
    if (m_openPrefabCallback) {
        return m_openPrefabCallback(absolutePath);
    }

    QMessageBox::warning(m_mainWindow,
        QObject::tr("Cannot Open Prefab"),
        QObject::tr("Prefab editor callback not configured."));
    return false;
}

bool EditorAssetActions::openCharacter(const QString& absolutePath)
{
    if (m_openCharacterCallback) {
        return m_openCharacterCallback(absolutePath);
    }

    QMessageBox::warning(m_mainWindow,
        QObject::tr("Cannot Open Character"),
        QObject::tr("Character editor callback not configured."));
    return false;
}

bool EditorAssetActions::openMaterial(const QString& absolutePath)
{
    return OpenMaterialGraphEditorDialog(m_mainWindow, absolutePath);
}

bool EditorAssetActions::openHUI(const QString& absolutePath)
{
    return OpenHUIVisualEditorDialog(m_mainWindow, absolutePath);
}

bool EditorAssetActions::openScript(const QString& absolutePath)
{
    if (m_openScriptCallback && m_openScriptCallback(absolutePath)) return true;
    return openInIDEAbsolute(absolutePath);
}

bool EditorAssetActions::openText(const QString& absolutePath)
{
    // Simple text viewer dialog
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(m_mainWindow,
            QObject::tr("Cannot Open File"),
            QObject::tr("Failed to open file: %1").arg(absolutePath));
        return false;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Create a simple viewer dialog
    QDialog* dialog = new QDialog(m_mainWindow);
    dialog->setWindowTitle(QFileInfo(absolutePath).fileName());
    dialog->resize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    
    QTextEdit* textEdit = new QTextEdit(dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(content);
    textEdit->setFont(QFont("Consolas", 10));
    layout->addWidget(textEdit);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Open, dialog);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttonBox->button(QDialogButtonBox::Open), &QPushButton::clicked, 
        [absolutePath, dialog]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
            dialog->accept();
        });
    layout->addWidget(buttonBox);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();

    return true;
}

bool EditorAssetActions::openExternal(const QString& absolutePath)
{
    // Use system default handler
    QUrl fileUrl = QUrl::fromLocalFile(absolutePath);
    bool success = QDesktopServices::openUrl(fileUrl);
    
    if (!success) {
        QMessageBox::warning(m_mainWindow,
            QObject::tr("Cannot Open File"),
            QObject::tr("Failed to open file with system default application:\n%1").arg(absolutePath));
    }
    
    return success;
}

bool EditorAssetActions::openInIDEAbsolute(const QString& absolutePath)
{
    return openExternal(absolutePath);
}

void EditorAssetActions::showNotImplemented(const QString& typeName, const QString& path)
{
    QMessageBox::information(m_mainWindow,
        QObject::tr("Not Implemented"),
        QObject::tr("%1 is not yet implemented.\n\nFile: %2\n\nYou can open this file externally.")
            .arg(typeName)
            .arg(QFileInfo(path).fileName()));
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
