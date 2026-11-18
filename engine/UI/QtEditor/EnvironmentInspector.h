#pragma once

/**
 * @file EnvironmentInspector.h
 * @brief Inspector widget for the EnvironmentComponent in the Details panel.
 *
 * Exposes background mode (Solid Color / HDRI), color picker, HDR file picker,
 * intensity, rotation yaw, and IBL toggle.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QColor>

// Forward declarations
struct EnvironmentComponent;

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

/**
 * @brief Collapsible inspector section for EnvironmentComponent.
 */
class EnvironmentInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit EnvironmentInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~EnvironmentInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void updateModeVisibility();
    void blockAllSignals(bool block);

    // Pickers / reset
    void pickSolidColor();
    void pickPostFilter();
    void browseHDRI();
    void reloadHDRI();
    void resetIntensity();
    void resetReflectionIntensity();
    void resetRotation();

    QComboBox*      m_modeCombo = nullptr;

    // Solid color
    QWidget*        m_solidColorRow = nullptr;
    QPushButton*    m_solidColorBtn = nullptr;
    QColor          m_currentSolidColor;

    // HDRI
    QWidget*        m_hdriGroup = nullptr;
    QLineEdit*      m_hdriPathEdit = nullptr;
    QPushButton*    m_hdriChooseBtn = nullptr;
    QPushButton*    m_hdriReloadBtn = nullptr;
    QDoubleSpinBox* m_hdriIntensitySpin = nullptr;
    QPushButton*    m_resetIntensityBtn = nullptr;
    QDoubleSpinBox* m_reflectionIntensitySpin = nullptr;
    QPushButton*    m_resetReflectionIntensityBtn = nullptr;
    QDoubleSpinBox* m_rotationYawSpin = nullptr;
    QPushButton*    m_resetRotationBtn = nullptr;
    QCheckBox*      m_useAsIBLCheck = nullptr;

    // Render distance
    QCheckBox*      m_enableRenderDistCheck = nullptr;
    QCheckBox*      m_infiniteRenderDistCheck = nullptr;
    QWidget*        m_renderDistGroup = nullptr;
    QDoubleSpinBox* m_renderDistSpin = nullptr;

    // Post processing
    QDoubleSpinBox* m_postSpins[9] = {};
    QPushButton*    m_postFilterBtn = nullptr;
    QColor          m_postFilter;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
