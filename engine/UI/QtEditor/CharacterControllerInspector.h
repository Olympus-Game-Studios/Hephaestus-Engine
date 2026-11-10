#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace Hephaestus {
namespace QtEditor {

class CharacterControllerInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit CharacterControllerInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void updateRuntimeReadout();
    QDoubleSpinBox* addSpin(QFormLayout* form, const QString& label, double min, double max, double step);

    QCheckBox* m_enabled = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_radius = nullptr;
    QDoubleSpinBox* m_crouchedHeight = nullptr;
    QDoubleSpinBox* m_proneHeight = nullptr;
    QDoubleSpinBox* m_walkSpeed = nullptr;
    QDoubleSpinBox* m_acceleration = nullptr;
    QDoubleSpinBox* m_deceleration = nullptr;
    QDoubleSpinBox* m_airAcceleration = nullptr;
    QDoubleSpinBox* m_airControl = nullptr;
    QDoubleSpinBox* m_jumpSpeed = nullptr;
    QDoubleSpinBox* m_coyoteTime = nullptr;
    QDoubleSpinBox* m_jumpBufferTime = nullptr;
    QDoubleSpinBox* m_gravityScale = nullptr;
    QDoubleSpinBox* m_maxFallSpeed = nullptr;
    QDoubleSpinBox* m_groundSnap = nullptr;
    QDoubleSpinBox* m_maxSlope = nullptr;
    QDoubleSpinBox* m_stepHeight = nullptr;
    QComboBox* m_layer = nullptr;
    QLabel* m_layerWarning = nullptr;
    QDoubleSpinBox* m_mass = nullptr;
    QDoubleSpinBox* m_pushStrength = nullptr;
    QLabel* m_grounded = nullptr;
    QLabel* m_groundState = nullptr;
    QLabel* m_velocity = nullptr;
    QLabel* m_groundVelocity = nullptr;
    QLabel* m_support = nullptr;
    QLabel* m_crouched = nullptr;
    QLabel* m_standBlocked = nullptr;
    QPushButton* m_remove = nullptr;
    QTimer m_runtimeTimer;
    bool m_updating = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
