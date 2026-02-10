#pragma once

/**
 * @file LightInspector.h
 * @brief Inspector widget for the LightComponent in the Details panel.
 *
 * Shows/edits light type, color, intensity, shadow settings, and
 * type-specific fields (range, cone angles). Updates the ECS component
 * immediately and marks the scene dirty.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QColor>
#include <entt/entt.hpp>

// Forward declarations
struct LightComponent;

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

/**
 * @brief Collapsible inspector section for LightComponent.
 *
 * Created/destroyed by the Details panel each time selection changes
 * (only if the selected entity has a LightComponent).
 */
class LightInspector : public QGroupBox
{
    Q_OBJECT

public:
    explicit LightInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~LightInspector() override = default;

    /** Re-read values from the component into the UI (e.g. after external edit). */
    void Refresh();

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void updateTypeVisibility();
    void blockAllSignals(bool block);

    // Reset helpers
    void resetColor();
    void resetIntensity();
    void resetRange();
    void resetInnerCone();
    void resetOuterCone();
    void resetShadowBias();
    void resetShadowNormalBias();
    void resetShadowStrength();

    /** Open QColorDialog and apply result. */
    void pickColor();

    // Context
    EditorContext* m_ctx = nullptr;
    entt::entity  m_entity = entt::null;
    bool          m_updating = false;

    // Widgets — General
    QCheckBox*      m_enabledCheck = nullptr;
    QComboBox*      m_typeCombo = nullptr;

    // Color
    QPushButton*    m_colorBtn = nullptr;
    QColor          m_currentColor;

    // Intensity
    QDoubleSpinBox* m_intensitySpin = nullptr;
    QPushButton*    m_resetIntensity = nullptr;

    // Range (Point/Spot)
    QWidget*        m_rangeRow = nullptr;
    QDoubleSpinBox* m_rangeSpin = nullptr;
    QPushButton*    m_resetRange = nullptr;

    // Spot cone angles
    QWidget*        m_innerConeRow = nullptr;
    QDoubleSpinBox* m_innerConeSpin = nullptr;
    QPushButton*    m_resetInnerCone = nullptr;

    QWidget*        m_outerConeRow = nullptr;
    QDoubleSpinBox* m_outerConeSpin = nullptr;
    QPushButton*    m_resetOuterCone = nullptr;

    // Shadows
    QCheckBox*      m_castShadowsCheck = nullptr;
    QWidget*        m_shadowGroup = nullptr;
    QDoubleSpinBox* m_shadowBiasSpin = nullptr;
    QPushButton*    m_resetShadowBias = nullptr;
    QDoubleSpinBox* m_shadowNormalBiasSpin = nullptr;
    QPushButton*    m_resetShadowNormalBias = nullptr;
    QDoubleSpinBox* m_shadowStrengthSpin = nullptr;
    QPushButton*    m_resetShadowStrength = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
