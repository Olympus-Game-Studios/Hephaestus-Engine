/**
 * @file EditorAssetActions.cpp
 * @brief Implementation of asset action handlers.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorAssetActions.h"
#include "EditorMainWindow.h"
#include "../../Assets/AssetManager.h"

#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
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
            // Could open a texture viewer in the future
            showNotImplemented("Texture viewer", absolutePath);
            return true;

        case AssetType::StaticMesh:
            // Mesh assets are placed into scenes via drag/drop in the editor.
            return false;

        case AssetType::Material:
            // Could open material editor in the future
            showNotImplemented("Material editor", absolutePath);
            return true;

        case AssetType::Audio:
            // Could open audio player in the future
            return openExternal(absolutePath);

        case AssetType::Blueprint:
            // Could open blueprint editor in the future
            showNotImplemented("Blueprint editor", absolutePath);
            return true;

        case AssetType::Shader:
            // Open shaders in external editor
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

bool EditorAssetActions::openScript(const QString& absolutePath)
{
    // Open in external editor (IDE, VS Code, etc.)
    return openExternal(absolutePath);
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
