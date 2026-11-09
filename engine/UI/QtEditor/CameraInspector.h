#pragma once

/**
 * @file CameraInspector.h
 * @brief Inspector widget for CameraComponent.
 *
 * Shows FOV, near/far plane, projection mode, active flag, and
 * editor mesh visibility toggle.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>

namespace Hephaestus {
namespace QtEditor {

class CameraInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit CameraInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~CameraInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void blockAllSignals(bool block);

    // Widgets
    QComboBox*      m_projCombo      = nullptr;
    QDoubleSpinBox* m_fovSpin        = nullptr;
    QDoubleSpinBox* m_viewModelFovSpin = nullptr;
    QDoubleSpinBox* m_nearSpin       = nullptr;
    QDoubleSpinBox* m_farSpin        = nullptr;
    QCheckBox*      m_activeCheck    = nullptr;
    QCheckBox*      m_showMeshCheck  = nullptr;
    QPushButton*    m_removeBtn      = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
