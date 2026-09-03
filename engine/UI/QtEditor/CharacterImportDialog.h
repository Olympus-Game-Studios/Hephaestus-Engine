#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Assets/CharacterImport.h"

#include <QDialog>
#include <QString>
#include <filesystem>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace Hephaestus {
namespace QtEditor {

class CharacterImportDialog : public QDialog
{
    Q_OBJECT
public:
    CharacterImportDialog(const std::filesystem::path& sourceGlb,
                          const std::filesystem::path& projectRoot,
                          const std::filesystem::path& destinationFolder,
                          QWidget* parent = nullptr);

    QString characterName() const;
    std::filesystem::path destinationFolder() const;
    bool createController() const { return m_createController; }
    bool importAsStaticMesh() const { return m_importAsStaticMesh; }
    SkeletalImportSettings importSettings() const;
    const CharacterInspectResult& inspectResult() const { return m_inspect; }

private:
    void refreshInspect();
    void onImport();
    void onImportAsStaticMesh();

    std::filesystem::path m_sourceGlb;
    std::filesystem::path m_projectRoot;
    CharacterInspectResult m_inspect;
    bool m_createController = true;
    bool m_importAsStaticMesh = false;

    QLabel* m_sourceLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    QTextEdit* m_messages = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_destEdit = nullptr;
    QCheckBox* m_controllerCheck = nullptr;
    QComboBox* m_spaceCombo = nullptr;
    QCheckBox* m_stripNamespaceCheck = nullptr;
    QPushButton* m_importButton = nullptr;
    QPushButton* m_staticMeshButton = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
