/**
 * @file EnvironmentInspector.cpp
 * @brief Implementation of the EnvironmentComponent inspector widget.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EnvironmentInspector.h"
#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <cmath>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Helpers (same sRGB <-> linear as LightInspector)
// ---------------------------------------------------------------------------

static int linToSrgb8(float v)
{
    v = std::max(0.0f, std::min(1.0f, v));
    float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(s * 255.0f + 0.5f);
}

static float srgb8ToLin(int c)
{
    float s = c / 255.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

static QColor vec3ToQColor(const Vec3& v)
{
    return QColor(linToSrgb8(v.x), linToSrgb8(v.y), linToSrgb8(v.z));
}

static Vec3 qcolorToVec3(const QColor& c)
{
    return Vec3(srgb8ToLin(c.red()), srgb8ToLin(c.green()), srgb8ToLin(c.blue()));
}

static QPushButton* makeResetBtn(QWidget* parent)
{
    auto* btn = new QPushButton(parent);
    btn->setIcon(btn->style()->standardIcon(QStyle::SP_BrowserReload));
    btn->setFixedSize(20, 20);
    btn->setToolTip("Reset to default");
    btn->setFlat(true);
    return btn;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EnvironmentInspector::EnvironmentInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : QGroupBox(tr("Environment"), parent)
    , m_ctx(ctx)
    , m_entity(entity)
{
    setCheckable(false);
    buildUI();
    connectSignals();
    Refresh();
}

// ---------------------------------------------------------------------------
// Build UI
// ---------------------------------------------------------------------------

void EnvironmentInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Mode combo
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem(tr("Solid Color"), static_cast<int>(EnvironmentComponent::BackgroundType::SolidColor));
    m_modeCombo->addItem(tr("Sky HDRI"),    static_cast<int>(EnvironmentComponent::BackgroundType::HDRI));
    form->addRow(tr("Mode:"), m_modeCombo);

    // ---- Solid color section ----
    {
        m_solidColorRow = new QWidget();
        auto* row = new QHBoxLayout(m_solidColorRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_solidColorBtn = new QPushButton();
        m_solidColorBtn->setFixedHeight(24);
        m_solidColorBtn->setToolTip(tr("Click to pick color"));
        auto* resetBtn = makeResetBtn(this);
        connect(resetBtn, &QPushButton::clicked, this, [this]() {
            m_currentSolidColor = QColor(26, 51, 77); // default ~(0.1,0.2,0.3) sRGB
            m_solidColorBtn->setStyleSheet(
                QString("background-color: %1; border: 1px solid #555;").arg(m_currentSolidColor.name()));
            m_solidColorBtn->setText(m_currentSolidColor.name().toUpper());
            applyToComponent();
        });
        row->addWidget(m_solidColorBtn, 1);
        row->addWidget(resetBtn);
        form->addRow(tr("Color:"), m_solidColorRow);
    }

    // ---- HDRI section ----
    m_hdriGroup = new QWidget();
    auto* hdriLayout = new QFormLayout(m_hdriGroup);
    hdriLayout->setContentsMargins(0, 0, 0, 0);
    hdriLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // File picker row
    {
        auto* fileRow = new QHBoxLayout();
        m_hdriPathEdit = new QLineEdit();
        m_hdriPathEdit->setReadOnly(true);
        m_hdriPathEdit->setPlaceholderText(tr("(No HDR file)"));
        m_hdriChooseBtn = new QPushButton(tr("Choose..."));
        m_hdriChooseBtn->setFixedWidth(70);
        m_hdriReloadBtn = new QPushButton();
        m_hdriReloadBtn->setIcon(m_hdriReloadBtn->style()->standardIcon(QStyle::SP_BrowserReload));
        m_hdriReloadBtn->setFixedSize(24, 24);
        m_hdriReloadBtn->setToolTip(tr("Reimport / Reload"));
        fileRow->addWidget(m_hdriPathEdit, 1);
        fileRow->addWidget(m_hdriChooseBtn);
        fileRow->addWidget(m_hdriReloadBtn);
        hdriLayout->addRow(tr("HDR File:"), static_cast<QLayout*>(fileRow));
    }

    // Intensity
    {
        auto* row = new QHBoxLayout();
        m_hdriIntensitySpin = new QDoubleSpinBox();
        m_hdriIntensitySpin->setRange(0.0, 100.0);
        m_hdriIntensitySpin->setSingleStep(0.1);
        m_hdriIntensitySpin->setDecimals(3);
        m_resetIntensityBtn = makeResetBtn(this);
        row->addWidget(m_hdriIntensitySpin, 1);
        row->addWidget(m_resetIntensityBtn);
        hdriLayout->addRow(tr("Intensity:"), static_cast<QLayout*>(row));
    }

    // Rotation yaw
    {
        auto* row = new QHBoxLayout();
        m_rotationYawSpin = new QDoubleSpinBox();
        m_rotationYawSpin->setRange(0.0, 360.0);
        m_rotationYawSpin->setSingleStep(1.0);
        m_rotationYawSpin->setDecimals(1);
        m_rotationYawSpin->setSuffix("°");
        m_rotationYawSpin->setWrapping(true);
        m_resetRotationBtn = makeResetBtn(this);
        row->addWidget(m_rotationYawSpin, 1);
        row->addWidget(m_resetRotationBtn);
        hdriLayout->addRow(tr("Rotation:"), static_cast<QLayout*>(row));
    }

    // Environment reflections
    m_useAsIBLCheck = new QCheckBox(tr("Environment Reflections"));
    hdriLayout->addRow(tr(""), m_useAsIBLCheck);

    {
        auto* row = new QHBoxLayout();
        m_reflectionIntensitySpin = new QDoubleSpinBox();
        m_reflectionIntensitySpin->setRange(0.0, 100.0);
        m_reflectionIntensitySpin->setSingleStep(0.1);
        m_reflectionIntensitySpin->setDecimals(3);
        m_reflectionIntensitySpin->setToolTip(tr("Global multiplier for HDRI reflections on PBR materials."));
        m_resetReflectionIntensityBtn = makeResetBtn(this);
        row->addWidget(m_reflectionIntensitySpin, 1);
        row->addWidget(m_resetReflectionIntensityBtn);
        hdriLayout->addRow(tr("Reflection Intensity:"), static_cast<QLayout*>(row));
    }

    form->addRow(m_hdriGroup);

    // ---- Render Distance section ----
    auto* renderDistSeparator = new QLabel(tr("<b>Render Distance</b>"));
    mainLayout->addWidget(renderDistSeparator);

    auto* renderDistForm = new QFormLayout();
    renderDistForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_enableRenderDistCheck = new QCheckBox(tr("Enable Render Distance"));
    m_enableRenderDistCheck->setToolTip(tr("Cull objects beyond a certain distance from the camera."));
    renderDistForm->addRow(m_enableRenderDistCheck);

    m_renderDistGroup = new QWidget();
    auto* rdLayout = new QFormLayout(m_renderDistGroup);
    rdLayout->setContentsMargins(12, 4, 0, 0);
    rdLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_infiniteRenderDistCheck = new QCheckBox(tr("Infinite"));
    m_infiniteRenderDistCheck->setToolTip(tr("Render all objects regardless of distance."));
    rdLayout->addRow(m_infiniteRenderDistCheck);

    m_renderDistSpin = new QDoubleSpinBox();
    m_renderDistSpin->setRange(10.0, 100000.0);
    m_renderDistSpin->setSingleStep(50.0);
    m_renderDistSpin->setDecimals(0);
    m_renderDistSpin->setToolTip(tr("Maximum distance at which objects are rendered."));
    rdLayout->addRow(tr("Distance:"), m_renderDistSpin);

    renderDistForm->addRow(m_renderDistGroup);
    mainLayout->addLayout(renderDistForm);

    mainLayout->addLayout(form);
}

// ---------------------------------------------------------------------------
// Connect signals
// ---------------------------------------------------------------------------

void EnvironmentInspector::connectSignals()
{
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyToComponent();
        updateModeVisibility();
    });
    connect(m_solidColorBtn, &QPushButton::clicked, this, &EnvironmentInspector::pickSolidColor);
    connect(m_hdriChooseBtn, &QPushButton::clicked, this, &EnvironmentInspector::browseHDRI);
    connect(m_hdriReloadBtn, &QPushButton::clicked, this, &EnvironmentInspector::reloadHDRI);
    connect(m_hdriIntensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_reflectionIntensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_rotationYawSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_useAsIBLCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });

    connect(m_enableRenderDistCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_renderDistGroup->setVisible(checked);
        applyToComponent();
    });
    connect(m_infiniteRenderDistCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_renderDistSpin->setEnabled(!checked);
        applyToComponent();
    });
    connect(m_renderDistSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });

    connect(m_resetIntensityBtn, &QPushButton::clicked, this, &EnvironmentInspector::resetIntensity);
    connect(m_resetReflectionIntensityBtn, &QPushButton::clicked, this, &EnvironmentInspector::resetReflectionIntensity);
    connect(m_resetRotationBtn, &QPushButton::clicked, this, &EnvironmentInspector::resetRotation);
}

// ---------------------------------------------------------------------------
// Refresh (component → UI)
// ---------------------------------------------------------------------------

void EnvironmentInspector::Refresh()
{
    setProperty("undoPushed", false);
    if (!m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<EnvironmentComponent>(m_entity)) return;

    const auto& env = reg.get<EnvironmentComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    int modeIdx = (env.type == EnvironmentComponent::BackgroundType::HDRI) ? 1 : 0;
    m_modeCombo->setCurrentIndex(modeIdx);

    // Solid color
    m_currentSolidColor = vec3ToQColor(env.solidColor);
    m_solidColorBtn->setStyleSheet(
        QString("background-color: %1; border: 1px solid #555;").arg(m_currentSolidColor.name()));
    m_solidColorBtn->setText(m_currentSolidColor.name().toUpper());

    // HDRI
    m_hdriPathEdit->setText(QString::fromStdString(env.hdriPath));
    m_hdriIntensitySpin->setValue(static_cast<double>(env.hdriIntensity));
    m_reflectionIntensitySpin->setValue(static_cast<double>(env.reflectionIntensity));
    m_rotationYawSpin->setValue(static_cast<double>(env.rotationYaw));
    m_useAsIBLCheck->setChecked(env.useAsIBL);

    // Render distance
    m_enableRenderDistCheck->setChecked(env.enableRenderDistance);
    m_infiniteRenderDistCheck->setChecked(env.infiniteRenderDistance);
    m_renderDistSpin->setValue(static_cast<double>(env.renderDistance));

    blockAllSignals(false);
    m_updating = false;

    updateModeVisibility();
    m_renderDistGroup->setVisible(env.enableRenderDistance);
    m_renderDistSpin->setEnabled(!env.infiniteRenderDistance);
}

// ---------------------------------------------------------------------------
// Apply (UI → component)
// ---------------------------------------------------------------------------

void EnvironmentInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<EnvironmentComponent>(m_entity)) return;

    if (!property("undoPushed").toBool()) {
        EditorUndoManager::PushSnapshot(m_ctx, "Edit environment");
        setProperty("undoPushed", true);
    }

    auto& env = reg.get<EnvironmentComponent>(m_entity);

    env.type = static_cast<EnvironmentComponent::BackgroundType>(m_modeCombo->currentData().toInt());
    env.solidColor = qcolorToVec3(m_currentSolidColor);
    env.hdriPath = m_hdriPathEdit->text().toStdString();
    env.hdriIntensity = static_cast<float>(m_hdriIntensitySpin->value());
    env.reflectionIntensity = static_cast<float>(m_reflectionIntensitySpin->value());
    env.rotationYaw = static_cast<float>(m_rotationYawSpin->value());
    env.useAsIBL = m_useAsIBLCheck->isChecked();

    env.enableRenderDistance = m_enableRenderDistCheck->isChecked();
    env.infiniteRenderDistance = m_infiniteRenderDistCheck->isChecked();
    env.renderDistance = static_cast<float>(m_renderDistSpin->value());

    m_ctx->MarkDirty();
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

void EnvironmentInspector::updateModeVisibility()
{
    bool isSolid = (m_modeCombo->currentIndex() == 0);
    m_solidColorRow->setVisible(isSolid);
    m_hdriGroup->setVisible(!isSolid);
}

void EnvironmentInspector::blockAllSignals(bool block)
{
    m_modeCombo->blockSignals(block);
    m_hdriIntensitySpin->blockSignals(block);
    m_reflectionIntensitySpin->blockSignals(block);
    m_rotationYawSpin->blockSignals(block);
    m_useAsIBLCheck->blockSignals(block);
    m_enableRenderDistCheck->blockSignals(block);
    m_infiniteRenderDistCheck->blockSignals(block);
    m_renderDistSpin->blockSignals(block);
}

// ---------------------------------------------------------------------------
// Pickers
// ---------------------------------------------------------------------------

void EnvironmentInspector::pickSolidColor()
{
    QColor chosen = QColorDialog::getColor(m_currentSolidColor, this, tr("Background Color"));
    if (chosen.isValid()) {
        m_currentSolidColor = chosen;
        m_solidColorBtn->setStyleSheet(
            QString("background-color: %1; border: 1px solid #555;").arg(m_currentSolidColor.name()));
        m_solidColorBtn->setText(m_currentSolidColor.name().toUpper());
        applyToComponent();
    }
}

void EnvironmentInspector::browseHDRI()
{
    QString startDir = m_hdriPathEdit->text().isEmpty() ? QString("assets/textures") : m_hdriPathEdit->text();
    QString filepath = QFileDialog::getOpenFileName(this, tr("Choose HDR/EXR Environment"),
        startDir,
        tr("HDR Images (*.hdr *.exr);;All Files (*)"));

    if (filepath.isEmpty()) return;

    m_hdriPathEdit->setText(filepath);
    applyToComponent();
}

void EnvironmentInspector::reloadHDRI()
{
    // Clearing and re-setting the path forces the renderer to reload the texture
    // (EngineFrameRenderer detects path changes and calls m_skybox.LoadHDRI())
    if (!m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<EnvironmentComponent>(m_entity)) return;

    auto& env = reg.get<EnvironmentComponent>(m_entity);
    std::string path = env.hdriPath;
    env.hdriPath = ""; // clear → renderer notices change
    m_ctx->MarkDirty();

    // Restore path on next tick so the renderer re-loads
    QMetaObject::invokeMethod(this, [this, path]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg2 = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg2.valid(m_entity) || !reg2.all_of<EnvironmentComponent>(m_entity)) return;
        reg2.get<EnvironmentComponent>(m_entity).hdriPath = path;
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void EnvironmentInspector::resetIntensity()  { m_hdriIntensitySpin->setValue(1.0); }
void EnvironmentInspector::resetReflectionIntensity() { m_reflectionIntensitySpin->setValue(1.0); }
void EnvironmentInspector::resetRotation()   { m_rotationYawSpin->setValue(0.0); }

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
