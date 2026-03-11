#if HEPHAESTUS_EDITOR_QT_UI

#include "BuildSettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

namespace Hephaestus {
namespace QtEditor {

BuildSettingsDialog::BuildSettingsDialog(const QString& projectName,
                                         const QStringList& sceneFiles,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Build Settings"));
    setMinimumWidth(420);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // ── Scene Group ──────────────────────────────────────────────────────
    auto* sceneGroup = new QGroupBox(tr("Startup"), this);
    auto* sceneLayout = new QFormLayout(sceneGroup);

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
    }
    sceneLayout->addRow(tr("Start Scene:"), m_sceneCombo);

    mainLayout->addWidget(sceneGroup);

    // ── Window Group ─────────────────────────────────────────────────────
    auto* windowGroup = new QGroupBox(tr("Window"), this);
    auto* windowLayout = new QFormLayout(windowGroup);

    m_titleEdit = new QLineEdit(projectName, this);
    windowLayout->addRow(tr("Title:"), m_titleEdit);

    auto* sizeLayout = new QHBoxLayout();
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(320, 7680);
    m_widthSpin->setValue(1280);
    m_widthSpin->setSuffix(" px");
    sizeLayout->addWidget(m_widthSpin);

    sizeLayout->addWidget(new QLabel(QStringLiteral("\u00D7"), this)); // × symbol

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(240, 4320);
    m_heightSpin->setValue(720);
    m_heightSpin->setSuffix(" px");
    sizeLayout->addWidget(m_heightSpin);

    windowLayout->addRow(tr("Resolution:"), sizeLayout);

    m_fullscreenCheck = new QCheckBox(this);
    windowLayout->addRow(tr("Fullscreen:"), m_fullscreenCheck);

    m_vsyncCheck = new QCheckBox(this);
    m_vsyncCheck->setChecked(true);
    windowLayout->addRow(tr("VSync:"), m_vsyncCheck);

    mainLayout->addWidget(windowGroup);

    // ── Buttons ──────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);

    auto* buildBtn = new QPushButton(tr("Build && Export"), this);
    buildBtn->setDefault(true);
    buttons->addButton(buildBtn, QDialogButtonBox::AcceptRole);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttons);
}

BuildSettings BuildSettingsDialog::GetSettings() const
{
    BuildSettings s;
    if (m_sceneCombo->isEnabled() && m_sceneCombo->currentData().isValid()) {
        s.startScene = m_sceneCombo->currentData().toString();
    }
    s.windowTitle  = m_titleEdit->text();
    s.windowWidth  = m_widthSpin->value();
    s.windowHeight = m_heightSpin->value();
    s.fullscreen   = m_fullscreenCheck->isChecked();
    s.vsync        = m_vsyncCheck->isChecked();
    return s;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
