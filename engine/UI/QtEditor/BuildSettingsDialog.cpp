#if HEPHAESTUS_EDITOR_QT_UI

#include "BuildSettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>

namespace Hephaestus {
namespace QtEditor {

BuildSettingsDialog::BuildSettingsDialog(const QString& projectName,
                                         const QStringList& sceneFiles,
                                         const QString& startupScene,
                                         const QString& outputDirectory,
                                         const BuildSettings& defaults,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Build Project"));
    setMinimumWidth(520);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto* packageGroup = new QGroupBox(tr("Package"), this);
    auto* packageLayout = new QFormLayout(packageGroup);

    m_configurationCombo = new QComboBox(this);
    m_configurationCombo->addItem(tr("Development"),
        static_cast<int>(Build::BuildConfiguration::Development));
    m_configurationCombo->addItem(tr("Release"),
        static_cast<int>(Build::BuildConfiguration::Release));
    m_configurationCombo->setCurrentIndex(
        defaults.configuration == Build::BuildConfiguration::Release ? 1 : 0);
    packageLayout->addRow(tr("Configuration:"), m_configurationCombo);

    auto* platform = new QLabel(tr("Windows x64"), this);
    packageLayout->addRow(tr("Platform:"), platform);

    m_sceneCombo = new QComboBox(this);
    if (sceneFiles.isEmpty()) {
        m_sceneCombo->addItem(tr("(No scenes found)"));
        m_sceneCombo->setEnabled(false);
    } else {
        for (const QString& scene : sceneFiles) {
            // Display just the filename, store full relative path
            QString displayName = scene.section('/', -1);
            m_sceneCombo->addItem(displayName, scene);
        }
        const int startupIndex = m_sceneCombo->findData(startupScene);
        if (startupIndex >= 0) {
            m_sceneCombo->setCurrentIndex(startupIndex);
        }
    }
    auto* sceneRow = new QWidget(this);
    auto* sceneRowLayout = new QHBoxLayout(sceneRow);
    sceneRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* openScene = new QPushButton(tr("Open"), sceneRow);
    auto* revealScene = new QPushButton(tr("Reveal"), sceneRow);
    sceneRowLayout->addWidget(m_sceneCombo, 1);
    sceneRowLayout->addWidget(openScene);
    sceneRowLayout->addWidget(revealScene);
    connect(openScene, &QPushButton::clicked, this, [this]() {
        emit openStartupSceneRequested(m_sceneCombo->currentData().toString());
    });
    connect(revealScene, &QPushButton::clicked, this, [this]() {
        emit revealStartupSceneRequested(m_sceneCombo->currentData().toString());
    });
    packageLayout->addRow(tr("Startup Scene:"), sceneRow);

    auto* outputRow = new QWidget(this);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    m_outputEdit = new QLineEdit(outputDirectory, outputRow);
    auto* browseOutput = new QPushButton(tr("Browse..."), outputRow);
    outputLayout->addWidget(m_outputEdit, 1);
    outputLayout->addWidget(browseOutput);
    connect(browseOutput, &QPushButton::clicked, this, &BuildSettingsDialog::chooseOutputDirectory);
    packageLayout->addRow(tr("Output:"), outputRow);

    m_validateCheck = new QCheckBox(tr("Validate project first"), this);
    m_validateCheck->setChecked(defaults.validateFirst);
    packageLayout->addRow(QString(), m_validateCheck);
    m_openOutputCheck = new QCheckBox(tr("Open output folder when complete"), this);
    m_openOutputCheck->setChecked(defaults.openOutputFolder);
    packageLayout->addRow(QString(), m_openOutputCheck);

    connect(m_configurationCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_validateCheck->setChecked(index == 1);
    });

    mainLayout->addWidget(packageGroup);

    auto* windowGroup = new QGroupBox(tr("Advanced"), this);
    windowGroup->setCheckable(true);
    windowGroup->setChecked(false);
    auto* windowLayout = new QFormLayout(windowGroup);

    m_titleEdit = new QLineEdit(defaults.windowTitle.isEmpty() ? projectName : defaults.windowTitle, this);
    windowLayout->addRow(tr("Title:"), m_titleEdit);

    auto* sizeLayout = new QHBoxLayout();
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(320, 7680);
    m_widthSpin->setValue(defaults.windowWidth);
    m_widthSpin->setSuffix(" px");
    sizeLayout->addWidget(m_widthSpin);

    sizeLayout->addWidget(new QLabel(QStringLiteral("\u00D7"), this)); // × symbol

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(240, 4320);
    m_heightSpin->setValue(defaults.windowHeight);
    m_heightSpin->setSuffix(" px");
    sizeLayout->addWidget(m_heightSpin);

    windowLayout->addRow(tr("Resolution:"), sizeLayout);

    m_fullscreenCheck = new QCheckBox(this);
    m_fullscreenCheck->setChecked(defaults.fullscreen);
    windowLayout->addRow(tr("Fullscreen:"), m_fullscreenCheck);

    m_vsyncCheck = new QCheckBox(this);
    m_vsyncCheck->setChecked(defaults.vsync);
    windowLayout->addRow(tr("VSync:"), m_vsyncCheck);

    m_createZipCheck = new QCheckBox(this);
    m_createZipCheck->setChecked(defaults.createZip);
    windowLayout->addRow(tr("Create ZIP:"), m_createZipCheck);
    m_unusedAssetsCheck = new QCheckBox(this);
    m_unusedAssetsCheck->setChecked(defaults.reportUnusedAssets);
    m_unusedAssetsCheck->setToolTip(
        tr("Advisory only: Lua and native code may load assets dynamically."));
    windowLayout->addRow(tr("Report possibly unused assets:"), m_unusedAssetsCheck);

    mainLayout->addWidget(windowGroup);

    // ── Buttons ──────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);

    auto* validateBtn = new QPushButton(tr("Validate"), this);
    auto* packageBtn = new QPushButton(tr("Package"), this);
    auto* packageRunBtn = new QPushButton(tr("Package && Run"), this);
    packageBtn->setDefault(true);
    buttons->addButton(validateBtn, QDialogButtonBox::ActionRole);
    buttons->addButton(packageBtn, QDialogButtonBox::ActionRole);
    buttons->addButton(packageRunBtn, QDialogButtonBox::ActionRole);

    connect(validateBtn, &QPushButton::clicked, this, [this]() {
        acceptAction(Build::BuildAction::Validate);
    });
    connect(packageBtn, &QPushButton::clicked, this, [this]() {
        acceptAction(Build::BuildAction::Package);
    });
    connect(packageRunBtn, &QPushButton::clicked, this, [this]() {
        acceptAction(Build::BuildAction::PackageAndRun);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttons);
}

BuildSettings BuildSettingsDialog::GetSettings() const
{
    BuildSettings s;
    s.configuration = static_cast<Build::BuildConfiguration>(
        m_configurationCombo->currentData().toInt());
    s.action = m_action;
    if (m_sceneCombo->isEnabled() && m_sceneCombo->currentData().isValid()) {
        s.startScene = m_sceneCombo->currentData().toString();
    }
    s.outputDirectory = m_outputEdit->text().trimmed();
    s.windowTitle  = m_titleEdit->text();
    s.windowWidth  = m_widthSpin->value();
    s.windowHeight = m_heightSpin->value();
    s.fullscreen   = m_fullscreenCheck->isChecked();
    s.vsync        = m_vsyncCheck->isChecked();
    s.validateFirst = m_validateCheck->isChecked();
    s.openOutputFolder = m_openOutputCheck->isChecked();
    s.createZip = m_createZipCheck->isChecked();
    s.reportUnusedAssets = m_unusedAssetsCheck->isChecked();
    return s;
}

void BuildSettingsDialog::chooseOutputDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Choose Package Output"), m_outputEdit->text());
    if (!directory.isEmpty()) {
        m_outputEdit->setText(directory);
    }
}

void BuildSettingsDialog::acceptAction(Build::BuildAction action)
{
    if (m_outputEdit->text().trimmed().isEmpty() && action != Build::BuildAction::Validate) {
        m_outputEdit->setFocus();
        return;
    }
    m_action = action;
    accept();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
