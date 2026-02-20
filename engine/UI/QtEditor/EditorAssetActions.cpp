/**
 * @file EditorAssetActions.cpp
 * @brief Implementation of asset action handlers.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorAssetActions.h"
#include "EditorMainWindow.h"
#include "../../Assets/AssetManager.h"
#include "../MaterialEditor/MaterialAssetSerializer.h"
#include "../MaterialEditor/MaterialGraphEvaluator.h"

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
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
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
            return openMaterial(absolutePath);

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

bool EditorAssetActions::openMaterial(const QString& absolutePath)
{
    namespace fs = std::filesystem;

    MaterialComponent material;
    MaterialEditor::Graph graph;
    std::string displayName;
    std::string error;

    if (!MaterialEditor::LoadMaterialAssetCompat(fs::path(absolutePath.toStdString()), material, displayName, graph, error)) {
        QMessageBox::warning(m_mainWindow,
            QObject::tr("Cannot Open Material"),
            QObject::tr("Failed to load material:\n%1").arg(QString::fromStdString(error)));
        return false;
    }

    if (graph.HasSourceGraph() && !graph.IsLegacyMode()) {
        MaterialComponent evaluated = material;
        std::string evalError;
        if (MaterialEditor::EvaluateGraph(graph, evaluated, evalError)) {
            material = evaluated;
        }
    }

    QDialog* dialog = new QDialog(m_mainWindow);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Material Editor - %1").arg(QFileInfo(absolutePath).fileName()));
    dialog->resize(520, 620);

    auto* rootLayout = new QVBoxLayout(dialog);

    auto* warningLabel = new QLabel(dialog);
    warningLabel->setWordWrap(true);
    if (graph.IsLegacyMode()) {
        warningLabel->setText(QObject::tr("Legacy PBR mode: graph is missing/invalid. Editing scalar/texture fields directly."));
    } else if (graph.HasSourceGraph()) {
        warningLabel->setText(QObject::tr("Graph-backed material detected. This Qt editor currently edits PBR fields; graph data is preserved on save."));
    } else {
        warningLabel->setText(QObject::tr("PBR material fields."));
    }
    rootLayout->addWidget(warningLabel);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* nameEdit = new QLineEdit(QString::fromStdString(displayName.empty() ? QFileInfo(absolutePath).completeBaseName().toStdString() : displayName), dialog);
    form->addRow(QObject::tr("Name"), nameEdit);

    auto makeSpin = [dialog](double min, double max, double step, double value) {
        auto* spin = new QDoubleSpinBox(dialog);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(4);
        spin->setValue(value);
        return spin;
    };

    auto* albedoX = makeSpin(0.0, 10.0, 0.01, material.albedo.x);
    auto* albedoY = makeSpin(0.0, 10.0, 0.01, material.albedo.y);
    auto* albedoZ = makeSpin(0.0, 10.0, 0.01, material.albedo.z);
    auto* albedoRow = new QWidget(dialog);
    auto* albedoLayout = new QHBoxLayout(albedoRow);
    albedoLayout->setContentsMargins(0, 0, 0, 0);
    albedoLayout->addWidget(albedoX); albedoLayout->addWidget(albedoY); albedoLayout->addWidget(albedoZ);
    form->addRow(QObject::tr("Albedo (RGB)"), albedoRow);

    auto* metallicSpin = makeSpin(0.0, 1.0, 0.01, material.metallic);
    auto* roughnessSpin = makeSpin(0.0, 1.0, 0.01, material.roughness);
    auto* aoSpin = makeSpin(0.0, 1.0, 0.01, material.ao);
    auto* emissiveX = makeSpin(0.0, 50.0, 0.01, material.emissive.x);
    auto* emissiveY = makeSpin(0.0, 50.0, 0.01, material.emissive.y);
    auto* emissiveZ = makeSpin(0.0, 50.0, 0.01, material.emissive.z);
    auto* emissiveStrengthSpin = makeSpin(0.0, 100.0, 0.01, material.emissiveStrength);
    auto* worldScaleSpin = makeSpin(0.001, 100.0, 0.01, material.worldTextureScale);

    form->addRow(QObject::tr("Metallic"), metallicSpin);
    form->addRow(QObject::tr("Roughness"), roughnessSpin);
    form->addRow(QObject::tr("AO"), aoSpin);

    auto* emissiveRow = new QWidget(dialog);
    auto* emissiveLayout = new QHBoxLayout(emissiveRow);
    emissiveLayout->setContentsMargins(0, 0, 0, 0);
    emissiveLayout->addWidget(emissiveX); emissiveLayout->addWidget(emissiveY); emissiveLayout->addWidget(emissiveZ);
    form->addRow(QObject::tr("Emissive (RGB)"), emissiveRow);
    form->addRow(QObject::tr("Emissive Strength"), emissiveStrengthSpin);

    auto* useAlbedoTex = new QCheckBox(QObject::tr("Use Albedo Texture"), dialog);
    useAlbedoTex->setChecked(material.useAlbedoTexture);
    auto* albedoPathEdit = new QLineEdit(QString::fromStdString(material.albedoTexturePath), dialog);
    form->addRow(useAlbedoTex, albedoPathEdit);

    auto* useNormalTex = new QCheckBox(QObject::tr("Use Normal Texture"), dialog);
    useNormalTex->setChecked(material.useNormalTexture);
    auto* normalPathEdit = new QLineEdit(QString::fromStdString(material.normalTexturePath), dialog);
    form->addRow(useNormalTex, normalPathEdit);

    auto* useRoughTex = new QCheckBox(QObject::tr("Use Roughness Texture"), dialog);
    useRoughTex->setChecked(material.useRoughnessTexture);
    auto* roughPathEdit = new QLineEdit(QString::fromStdString(material.roughnessTexturePath), dialog);
    form->addRow(useRoughTex, roughPathEdit);

    auto* useAoTex = new QCheckBox(QObject::tr("Use AO Texture"), dialog);
    useAoTex->setChecked(material.useAoTexture);
    auto* aoPathEdit = new QLineEdit(QString::fromStdString(material.aoTexturePath), dialog);
    form->addRow(useAoTex, aoPathEdit);

    auto* useWorldAligned = new QCheckBox(QObject::tr("Use World Aligned Texture"), dialog);
    useWorldAligned->setChecked(material.useWorldAlignedTexture);
    form->addRow(useWorldAligned, worldScaleSpin);

    rootLayout->addLayout(form);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    rootLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, dialog, [=]() {
        MaterialComponent toSave = material;
        toSave.albedo.x = static_cast<float>(albedoX->value());
        toSave.albedo.y = static_cast<float>(albedoY->value());
        toSave.albedo.z = static_cast<float>(albedoZ->value());
        toSave.metallic = static_cast<float>(metallicSpin->value());
        toSave.roughness = static_cast<float>(roughnessSpin->value());
        toSave.ao = static_cast<float>(aoSpin->value());
        toSave.emissive.x = static_cast<float>(emissiveX->value());
        toSave.emissive.y = static_cast<float>(emissiveY->value());
        toSave.emissive.z = static_cast<float>(emissiveZ->value());
        toSave.emissiveStrength = static_cast<float>(emissiveStrengthSpin->value());

        toSave.useAlbedoTexture = useAlbedoTex->isChecked();
        toSave.albedoTexturePath = albedoPathEdit->text().toStdString();
        toSave.useNormalTexture = useNormalTex->isChecked();
        toSave.normalTexturePath = normalPathEdit->text().toStdString();
        toSave.useRoughnessTexture = useRoughTex->isChecked();
        toSave.roughnessTexturePath = roughPathEdit->text().toStdString();
        toSave.useAoTexture = useAoTex->isChecked();
        toSave.aoTexturePath = aoPathEdit->text().toStdString();
        toSave.useWorldAlignedTexture = useWorldAligned->isChecked();
        toSave.worldTextureScale = static_cast<float>(worldScaleSpin->value());

        std::string saveError;
        const std::string newName = nameEdit->text().trimmed().isEmpty()
            ? QFileInfo(absolutePath).completeBaseName().toStdString()
            : nameEdit->text().toStdString();

        if (!MaterialEditor::SaveMaterialAssetCompat(fs::path(absolutePath.toStdString()), toSave, newName, graph, {}, saveError)) {
            QMessageBox::warning(dialog,
                QObject::tr("Save Failed"),
                QObject::tr("Failed to save material:\n%1").arg(QString::fromStdString(saveError)));
            return;
        }

        dialog->accept();
    });

    dialog->show();
    return true;
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
