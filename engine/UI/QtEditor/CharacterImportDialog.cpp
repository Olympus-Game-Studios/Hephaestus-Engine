#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterImportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QObject>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

CharacterImportDialog::CharacterImportDialog(const std::filesystem::path& sourceGlb,
                                             const std::filesystem::path& projectRoot,
                                             const std::filesystem::path& destinationFolder,
                                             QWidget* parent)
    : QDialog(parent)
    , m_sourceGlb(sourceGlb)
    , m_projectRoot(projectRoot)
{
    setWindowTitle(tr("Import Character"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_sourceLabel = new QLabel(this);
    m_sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sourceLabel->setWordWrap(true);
    form->addRow(tr("Source:"), m_sourceLabel);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setWordWrap(true);
    form->addRow(tr("Detected:"), m_statsLabel);

    m_nameEdit = new QLineEdit(QString::fromStdString(sourceGlb.stem().string()), this);
    form->addRow(tr("Character Name:"), m_nameEdit);

    auto* destRow = new QWidget(this);
    auto* destLayout = new QHBoxLayout(destRow);
    destLayout->setContentsMargins(0, 0, 0, 0);
    m_destEdit = new QLineEdit(QString::fromStdString(destinationFolder.generic_string()), this);
    auto* browse = new QPushButton(tr("Browse…"), destRow);
    destLayout->addWidget(m_destEdit, 1);
    destLayout->addWidget(browse);
    form->addRow(tr("Destination:"), destRow);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Destination Folder"), m_destEdit->text());
        if (!dir.isEmpty()) {
            m_destEdit->setText(dir);
        }
    });

    m_controllerCheck = new QCheckBox(tr("Create character controller"), this);
    m_controllerCheck->setChecked(true);
    form->addRow(QString(), m_controllerCheck);

    m_spaceCombo = new QComboBox(this);
    m_spaceCombo->addItem(tr("Auto"), static_cast<int>(FbxSpaceConversion::Auto));
    m_spaceCombo->addItem(tr("Adjust Transforms"), static_cast<int>(FbxSpaceConversion::AdjustTransforms));
    m_spaceCombo->addItem(tr("Modify Geometry"), static_cast<int>(FbxSpaceConversion::ModifyGeometry));
    form->addRow(tr("FBX Space Conversion:"), m_spaceCombo);
    m_spaceCombo->setVisible(CharacterImporter::IsFbxPath(m_sourceGlb));
    connect(m_spaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshInspect(); });

    m_stripNamespaceCheck = new QCheckBox(tr("Strip bone namespace (mixamorig:Name → Name)"), this);
    m_stripNamespaceCheck->setChecked(false);
    form->addRow(QString(), m_stripNamespaceCheck);
    m_stripNamespaceCheck->setVisible(CharacterImporter::IsFbxPath(m_sourceGlb));
    connect(m_stripNamespaceCheck, &QCheckBox::toggled, this, [this](bool) { refreshInspect(); });
    layout->addLayout(form);

    m_messages = new QTextEdit(this);
    m_messages->setReadOnly(true);
    m_messages->setMinimumHeight(90);
    layout->addWidget(m_messages);

    auto* buttons = new QDialogButtonBox(this);
    m_importButton = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    m_staticMeshButton = buttons->addButton(tr("Import as Static Mesh…"),
                                            QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(m_importButton, &QPushButton::clicked, this, &CharacterImportDialog::onImport);
    connect(m_staticMeshButton, &QPushButton::clicked, this,
            &CharacterImportDialog::onImportAsStaticMesh);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshInspect();
}

QString CharacterImportDialog::characterName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

std::filesystem::path CharacterImportDialog::destinationFolder() const
{
    return std::filesystem::path(m_destEdit->text().toStdString());
}

SkeletalImportSettings CharacterImportDialog::importSettings() const
{
    SkeletalImportSettings settings;
    if (m_spaceCombo) {
        settings.fbxSpaceConversion =
            static_cast<FbxSpaceConversion>(m_spaceCombo->currentData().toInt());
    }
    if (m_stripNamespaceCheck) {
        settings.stripBoneNamespace = m_stripNamespaceCheck->isChecked();
    }
    return settings;
}

void CharacterImportDialog::refreshInspect()
{
    m_sourceLabel->setText(QString::fromStdString(m_sourceGlb.generic_string()));
    std::string error;
    const bool ok = CharacterImporter::Inspect(m_sourceGlb, importSettings(), m_inspect, error);
    if (!ok) {
        m_statsLabel->setText(tr("Cannot import as a character."));
        m_messages->setPlainText(QString::fromStdString(error));
        m_importButton->setEnabled(false);
        m_staticMeshButton->setVisible(CharacterImporter::IsCharacterSourcePath(m_sourceGlb));
        return;
    }

    m_statsLabel->setText(
        tr("Source %1  •  Mesh %2  •  Skin %3  •  %4 bones  •  %5 clip(s)  •  %6 material slot(s)")
            .arg(QString::fromStdString(m_inspect.sourceFormat))
            .arg(QString::fromStdString(m_inspect.meshName))
            .arg(QString::fromStdString(m_inspect.skinName))
            .arg(m_inspect.boneCount)
            .arg(m_inspect.clipCount)
            .arg(m_inspect.materialSlotCount));
    QStringList lines;
    for (const std::string& warning : m_inspect.warnings) {
        lines << QString::fromStdString(warning);
    }
    m_messages->setPlainText(lines.isEmpty() ? tr("Ready to import.") : lines.join('\n'));
    m_importButton->setEnabled(true);
    m_staticMeshButton->setVisible(false);
}

void CharacterImportDialog::onImport()
{
    m_createController = m_controllerCheck->isChecked();
    m_importAsStaticMesh = false;
    if (characterName().isEmpty()) {
        m_messages->setPlainText(tr("Character name is empty."));
        return;
    }
    accept();
}

void CharacterImportDialog::onImportAsStaticMesh()
{
    m_importAsStaticMesh = true;
    accept();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
