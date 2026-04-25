#pragma once

/**
 * @file EnvironmentInspector.h
 * @brief Inspector widget for the EnvironmentComponent in the Details panel.
 *
 * Exposes background mode (Solid Color / HDRI), color picker, HDR file picker,
 * intensity, rotation yaw, and IBL toggle.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QColor>
#include <entt/entt.hpp>

// Forward declarations
struct EnvironmentComponent;

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

/**
 * @brief Collapsible inspector section for EnvironmentComponent.
 */
class EnvironmentInspector : public QGroupBox
{
    Q_OBJECT

public:
    explicit EnvironmentInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~EnvironmentInspector() override = default;

    /** Re-read values from the component into the UI. */
    void Refresh();

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void updateModeVisibility();
    void blockAllSignals(bool block);

    // Pickers / reset
    void pickSolidColor();
    void browseHDRI();
    void reloadHDRI();
    void resetIntensity();
    void resetReflectionIntensity();
    void resetRotation();

    // Context
    EditorContext* m_ctx = nullptr;
    entt::entity  m_entity = entt::null;
    bool          m_updating = false;

    // Mode
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
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
