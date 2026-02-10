/**
 * @file LightInspector.cpp
 * @brief Implementation of the LightComponent inspector widget.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "LightInspector.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QColorDialog>
#include <QStyle>
#include <cmath>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979f;
static constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;

/** Linear float [0,1] → sRGB [0,255] int. */
static int linearToSrgb8(float v)
{
    v = std::max(0.0f, std::min(1.0f, v));
    float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(s * 255.0f + 0.5f);
}

/** sRGB [0,255] int → linear float [0,1]. */
static float srgb8ToLinear(int c)
{
    float s = c / 255.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

static QColor vec3ToQColor(const Vec3& v)
{
    return QColor(linearToSrgb8(v.x), linearToSrgb8(v.y), linearToSrgb8(v.z));
}

static Vec3 qcolorToVec3(const QColor& c)
{
    return Vec3(srgb8ToLinear(c.red()), srgb8ToLinear(c.green()), srgb8ToLinear(c.blue()));
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

LightInspector::LightInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : QGroupBox(tr("Light"), parent)
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

void LightInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Enabled + Type row
    {
        auto* row = new QHBoxLayout();
        m_enabledCheck = new QCheckBox(tr("Enabled"));
        m_typeCombo = new QComboBox();
        m_typeCombo->addItem(tr("Directional"), static_cast<int>(LightType::Directional));
        m_typeCombo->addItem(tr("Point"),       static_cast<int>(LightType::Point));
        m_typeCombo->addItem(tr("Spot"),        static_cast<int>(LightType::Spot));
        row->addWidget(m_enabledCheck);
        row->addWidget(m_typeCombo, 1);
        form->addRow(tr(""), static_cast<QLayout*>(row));
    }

    // Color
    {
        auto* row = new QHBoxLayout();
        m_colorBtn = new QPushButton();
        m_colorBtn->setFixedHeight(24);
        m_colorBtn->setToolTip(tr("Click to pick color"));
        row->addWidget(m_colorBtn, 1);
        auto* resetBtn = makeResetBtn(this);
        connect(resetBtn, &QPushButton::clicked, this, &LightInspector::resetColor);
        row->addWidget(resetBtn);
        form->addRow(tr("Color:"), static_cast<QLayout*>(row));
    }

    // Intensity
    {
        auto* row = new QHBoxLayout();
        m_intensitySpin = new QDoubleSpinBox();
        m_intensitySpin->setRange(0.0, 100000.0);
        m_intensitySpin->setSingleStep(0.1);
        m_intensitySpin->setDecimals(3);
        m_resetIntensity = makeResetBtn(this);
        row->addWidget(m_intensitySpin, 1);
        row->addWidget(m_resetIntensity);
        form->addRow(tr("Intensity:"), static_cast<QLayout*>(row));
    }

    // Range (Point/Spot only)
    {
        m_rangeRow = new QWidget();
        auto* row = new QHBoxLayout(m_rangeRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_rangeSpin = new QDoubleSpinBox();
        m_rangeSpin->setRange(0.01, 100000.0);
        m_rangeSpin->setSingleStep(0.5);
        m_rangeSpin->setDecimals(3);
        m_resetRange = makeResetBtn(this);
        row->addWidget(m_rangeSpin, 1);
        row->addWidget(m_resetRange);
        form->addRow(tr("Range:"), m_rangeRow);
    }

    // Inner cone angle (Spot only)
    {
        m_innerConeRow = new QWidget();
        auto* row = new QHBoxLayout(m_innerConeRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_innerConeSpin = new QDoubleSpinBox();
        m_innerConeSpin->setRange(0.0, 180.0);
        m_innerConeSpin->setSingleStep(1.0);
        m_innerConeSpin->setDecimals(1);
        m_innerConeSpin->setSuffix("°");
        m_resetInnerCone = makeResetBtn(this);
        row->addWidget(m_innerConeSpin, 1);
        row->addWidget(m_resetInnerCone);
        form->addRow(tr("Inner Cone:"), m_innerConeRow);
    }

    // Outer cone angle (Spot only)
    {
        m_outerConeRow = new QWidget();
        auto* row = new QHBoxLayout(m_outerConeRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_outerConeSpin = new QDoubleSpinBox();
        m_outerConeSpin->setRange(0.0, 180.0);
        m_outerConeSpin->setSingleStep(1.0);
        m_outerConeSpin->setDecimals(1);
        m_outerConeSpin->setSuffix("°");
        m_resetOuterCone = makeResetBtn(this);
        row->addWidget(m_outerConeSpin, 1);
        row->addWidget(m_resetOuterCone);
        form->addRow(tr("Outer Cone:"), m_outerConeRow);
    }

    mainLayout->addLayout(form);

    // ---- Shadows section ----
    m_castShadowsCheck = new QCheckBox(tr("Cast Shadows"));
    mainLayout->addWidget(m_castShadowsCheck);

    m_shadowGroup = new QWidget();
    auto* shadowForm = new QFormLayout(m_shadowGroup);
    shadowForm->setContentsMargins(12, 4, 0, 0);
    shadowForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Shadow Bias
    {
        auto* row = new QHBoxLayout();
        m_shadowBiasSpin = new QDoubleSpinBox();
        m_shadowBiasSpin->setRange(0.0, 0.1);
        m_shadowBiasSpin->setSingleStep(0.0001);
        m_shadowBiasSpin->setDecimals(4);
        m_resetShadowBias = makeResetBtn(this);
        row->addWidget(m_shadowBiasSpin, 1);
        row->addWidget(m_resetShadowBias);
        shadowForm->addRow(tr("Bias:"), static_cast<QLayout*>(row));
    }

    // Shadow Normal Bias
    {
        auto* row = new QHBoxLayout();
        m_shadowNormalBiasSpin = new QDoubleSpinBox();
        m_shadowNormalBiasSpin->setRange(0.0, 1.0);
        m_shadowNormalBiasSpin->setSingleStep(0.005);
        m_shadowNormalBiasSpin->setDecimals(4);
        m_resetShadowNormalBias = makeResetBtn(this);
        row->addWidget(m_shadowNormalBiasSpin, 1);
        row->addWidget(m_resetShadowNormalBias);
        shadowForm->addRow(tr("Normal Bias:"), static_cast<QLayout*>(row));
    }

    // Shadow Strength
    {
        auto* row = new QHBoxLayout();
        m_shadowStrengthSpin = new QDoubleSpinBox();
        m_shadowStrengthSpin->setRange(0.0, 1.0);
        m_shadowStrengthSpin->setSingleStep(0.05);
        m_shadowStrengthSpin->setDecimals(3);
        m_resetShadowStrength = makeResetBtn(this);
        row->addWidget(m_shadowStrengthSpin, 1);
        row->addWidget(m_resetShadowStrength);
        shadowForm->addRow(tr("Strength:"), static_cast<QLayout*>(row));
    }

    mainLayout->addWidget(m_shadowGroup);
}

// ---------------------------------------------------------------------------
// Connect signals
// ---------------------------------------------------------------------------

void LightInspector::connectSignals()
{
    connect(m_enabledCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyToComponent();
        updateTypeVisibility();
    });
    connect(m_colorBtn, &QPushButton::clicked, this, &LightInspector::pickColor);
    connect(m_intensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_rangeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_innerConeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        // Ensure inner <= outer
        if (m_innerConeSpin->value() > m_outerConeSpin->value()) {
            m_outerConeSpin->blockSignals(true);
            m_outerConeSpin->setValue(m_innerConeSpin->value());
            m_outerConeSpin->blockSignals(false);
        }
        applyToComponent();
    });
    connect(m_outerConeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        // Ensure outer >= inner
        if (m_outerConeSpin->value() < m_innerConeSpin->value()) {
            m_innerConeSpin->blockSignals(true);
            m_innerConeSpin->setValue(m_outerConeSpin->value());
            m_innerConeSpin->blockSignals(false);
        }
        applyToComponent();
    });
    connect(m_castShadowsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_shadowGroup->setVisible(checked);
        applyToComponent();
    });
    connect(m_shadowBiasSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_shadowNormalBiasSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_shadowStrengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });

    // Reset buttons
    connect(m_resetIntensity, &QPushButton::clicked, this, &LightInspector::resetIntensity);
    connect(m_resetRange, &QPushButton::clicked, this, &LightInspector::resetRange);
    connect(m_resetInnerCone, &QPushButton::clicked, this, &LightInspector::resetInnerCone);
    connect(m_resetOuterCone, &QPushButton::clicked, this, &LightInspector::resetOuterCone);
    connect(m_resetShadowBias, &QPushButton::clicked, this, &LightInspector::resetShadowBias);
    connect(m_resetShadowNormalBias, &QPushButton::clicked, this, &LightInspector::resetShadowNormalBias);
    connect(m_resetShadowStrength, &QPushButton::clicked, this, &LightInspector::resetShadowStrength);
}

// ---------------------------------------------------------------------------
// Refresh (component → UI)
// ---------------------------------------------------------------------------

void LightInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<LightComponent>(m_entity)) return;

    const auto& lc = reg.get<LightComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    m_enabledCheck->setChecked(lc.enabled);

    // Type combo
    int idx = 0;
    switch (lc.type) {
        case LightType::Directional: idx = 0; break;
        case LightType::Point:       idx = 1; break;
        case LightType::Spot:        idx = 2; break;
    }
    m_typeCombo->setCurrentIndex(idx);

    // Color
    m_currentColor = vec3ToQColor(lc.color);
    m_colorBtn->setStyleSheet(
        QString("background-color: %1; border: 1px solid #555;").arg(m_currentColor.name()));
    m_colorBtn->setText(m_currentColor.name().toUpper());

    m_intensitySpin->setValue(static_cast<double>(lc.intensity));
    m_rangeSpin->setValue(static_cast<double>(lc.range));
    m_innerConeSpin->setValue(static_cast<double>(lc.innerConeAngle * RAD_TO_DEG));
    m_outerConeSpin->setValue(static_cast<double>(lc.outerConeAngle * RAD_TO_DEG));

    m_castShadowsCheck->setChecked(lc.castShadows);
    m_shadowBiasSpin->setValue(static_cast<double>(lc.shadowBias));
    m_shadowNormalBiasSpin->setValue(static_cast<double>(lc.shadowNormalBias));
    m_shadowStrengthSpin->setValue(static_cast<double>(lc.shadowStrength));

    blockAllSignals(false);
    m_updating = false;

    updateTypeVisibility();
    m_shadowGroup->setVisible(lc.castShadows);
}

// ---------------------------------------------------------------------------
// Apply (UI → component)
// ---------------------------------------------------------------------------

void LightInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<LightComponent>(m_entity)) return;

    auto& lc = reg.get<LightComponent>(m_entity);

    lc.enabled = m_enabledCheck->isChecked();
    lc.type = static_cast<LightType>(m_typeCombo->currentData().toInt());
    lc.color = qcolorToVec3(m_currentColor);
    lc.intensity = static_cast<float>(m_intensitySpin->value());
    lc.range = static_cast<float>(m_rangeSpin->value());
    lc.innerConeAngle = static_cast<float>(m_innerConeSpin->value()) * DEG_TO_RAD;
    lc.outerConeAngle = static_cast<float>(m_outerConeSpin->value()) * DEG_TO_RAD;
    lc.castShadows = m_castShadowsCheck->isChecked();
    lc.shadowBias = static_cast<float>(m_shadowBiasSpin->value());
    lc.shadowNormalBias = static_cast<float>(m_shadowNormalBiasSpin->value());
    lc.shadowStrength = static_cast<float>(m_shadowStrengthSpin->value());

    // Update editor icon if type changed
    if (reg.all_of<EditorIconComponent>(m_entity)) {
        reg.get<EditorIconComponent>(m_entity) = EditorIconComponent::ForLight(lc.type);
    }

    m_ctx->MarkDirty();
}

// ---------------------------------------------------------------------------
// Visibility helpers
// ---------------------------------------------------------------------------

void LightInspector::updateTypeVisibility()
{
    LightType lt = static_cast<LightType>(m_typeCombo->currentData().toInt());
    bool isPoint = (lt == LightType::Point);
    bool isSpot  = (lt == LightType::Spot);

    // Range row — Point + Spot
    m_rangeRow->setVisible(isPoint || isSpot);
    // Find the label in the form and hide it too
    if (auto* formLayout = qobject_cast<QFormLayout*>(m_rangeRow->parentWidget()->layout())) {
        // Get label widget for the range row
        for (int i = 0; i < formLayout->rowCount(); ++i) {
            auto* labelItem = formLayout->itemAt(i, QFormLayout::LabelRole);
            auto* fieldItem = formLayout->itemAt(i, QFormLayout::FieldRole);
            if (fieldItem && fieldItem->widget() == m_rangeRow && labelItem && labelItem->widget()) {
                labelItem->widget()->setVisible(isPoint || isSpot);
            }
        }
    }

    // Cone angles — Spot only
    m_innerConeRow->setVisible(isSpot);
    m_outerConeRow->setVisible(isSpot);
}

void LightInspector::blockAllSignals(bool block)
{
    m_enabledCheck->blockSignals(block);
    m_typeCombo->blockSignals(block);
    m_intensitySpin->blockSignals(block);
    m_rangeSpin->blockSignals(block);
    m_innerConeSpin->blockSignals(block);
    m_outerConeSpin->blockSignals(block);
    m_castShadowsCheck->blockSignals(block);
    m_shadowBiasSpin->blockSignals(block);
    m_shadowNormalBiasSpin->blockSignals(block);
    m_shadowStrengthSpin->blockSignals(block);
}

// ---------------------------------------------------------------------------
// Color picker
// ---------------------------------------------------------------------------

void LightInspector::pickColor()
{
    QColor chosen = QColorDialog::getColor(m_currentColor, this, tr("Light Color"));
    if (chosen.isValid()) {
        m_currentColor = chosen;
        m_colorBtn->setStyleSheet(
            QString("background-color: %1; border: 1px solid #555;").arg(m_currentColor.name()));
        m_colorBtn->setText(m_currentColor.name().toUpper());
        applyToComponent();
    }
}

// ---------------------------------------------------------------------------
// Reset-to-default helpers
// ---------------------------------------------------------------------------

void LightInspector::resetColor()
{
    m_currentColor = QColor(255, 255, 255);
    m_colorBtn->setStyleSheet(
        QString("background-color: %1; border: 1px solid #555;").arg(m_currentColor.name()));
    m_colorBtn->setText(m_currentColor.name().toUpper());
    applyToComponent();
}

void LightInspector::resetIntensity()    { m_intensitySpin->setValue(1.0); }
void LightInspector::resetRange()        { m_rangeSpin->setValue(10.0); }
void LightInspector::resetInnerCone()    { m_innerConeSpin->setValue(45.0); }
void LightInspector::resetOuterCone()    { m_outerConeSpin->setValue(55.0); }
void LightInspector::resetShadowBias()   { m_shadowBiasSpin->setValue(0.002); }
void LightInspector::resetShadowNormalBias() { m_shadowNormalBiasSpin->setValue(0.01); }
void LightInspector::resetShadowStrength()   { m_shadowStrengthSpin->setValue(0.7); }

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
