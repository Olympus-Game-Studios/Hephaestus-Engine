#pragma once

/**
 * @file PhysicsInspectors.h
 * @brief Inspector widgets for RigidBodyComponent, MeshColliderComponent,
 *        and PhysicsShapeComponent.
 *
 * Three classes in one header because they are tightly related and often
 * shown together.  Each follows the same pattern as LightInspector:
 *   - buildUI() creates widgets
 *   - connectSignals() wires Qt signals
 *   - Refresh() reads ECS → widgets
 *   - applyToComponent() writes widgets → ECS
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"
#include "../../Scene/Components.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace Hephaestus {
namespace QtEditor {

// ═══════════════════════════════════════════════════════════════════════════
// RigidBodyInspector
// ═══════════════════════════════════════════════════════════════════════════

class RigidBodyInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit RigidBodyInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~RigidBodyInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void applyPreset(bool isPlayer);
    void blockAllSignals(bool block);
    void updateDynamicFieldsVisibility();

    // Widgets
    QPushButton*    m_presetPlayerBtn  = nullptr;
    QPushButton*    m_presetGroundBtn  = nullptr;
    QComboBox*      m_motionCombo      = nullptr;
    QComboBox*      m_layerCombo       = nullptr;
    QLabel*         m_layerWarning     = nullptr;
    QCheckBox*      m_triggerCheck     = nullptr;
    QCheckBox*      m_gravityCheck     = nullptr;
    QCheckBox*      m_freezeRotCheck   = nullptr;
    QDoubleSpinBox* m_massSpin         = nullptr;
    QWidget*        m_massRow          = nullptr;
    QDoubleSpinBox* m_linearDampSpin   = nullptr;
    QDoubleSpinBox* m_angularDampSpin  = nullptr;
    QDoubleSpinBox* m_frictionSpin     = nullptr;
    QDoubleSpinBox* m_restitutionSpin  = nullptr;
    QDoubleSpinBox* m_linVelX = nullptr;
    QDoubleSpinBox* m_linVelY = nullptr;
    QDoubleSpinBox* m_linVelZ = nullptr;
    QDoubleSpinBox* m_angVelX = nullptr;
    QDoubleSpinBox* m_angVelY = nullptr;
    QDoubleSpinBox* m_angVelZ = nullptr;
    QCheckBox*      m_debugDrawCheck   = nullptr;
    QPushButton*    m_removeBtn        = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════════
// MeshColliderInspector
// ═══════════════════════════════════════════════════════════════════════════

class MeshColliderInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit MeshColliderInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~MeshColliderInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void blockAllSignals(bool block);

    // Widgets
    QCheckBox*   m_enabledCheck      = nullptr;
    QCheckBox*   m_staticCheck       = nullptr;
    QCheckBox*   m_triggerCheck      = nullptr;
    QCheckBox*   m_complexCheck      = nullptr;
    QCheckBox*   m_debugDrawCheck    = nullptr;
    QLabel*      m_contactsLabel     = nullptr;
    QPushButton* m_removeBtn         = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════════
// PhysicsShapeInspector
// ═══════════════════════════════════════════════════════════════════════════

class PhysicsShapeInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit PhysicsShapeInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~PhysicsShapeInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void updateShapeFieldsVisibility();
    void autoFitToMeshBounds(PhysicsShapeType newType);
    void blockAllSignals(bool block);

    // Widgets
    QComboBox*      m_shapeCombo     = nullptr;

    // Box
    QWidget*        m_boxGroup       = nullptr;
    QDoubleSpinBox* m_extX = nullptr;
    QDoubleSpinBox* m_extY = nullptr;
    QDoubleSpinBox* m_extZ = nullptr;

    // Sphere
    QWidget*        m_sphereGroup    = nullptr;
    QDoubleSpinBox* m_radiusSpin     = nullptr;

    // Capsule
    QWidget*        m_capsuleGroup   = nullptr;
    QDoubleSpinBox* m_capHeightSpin  = nullptr;
    QDoubleSpinBox* m_capRadiusSpin  = nullptr;

    // Offset (common)
    QDoubleSpinBox* m_offX = nullptr;
    QDoubleSpinBox* m_offY = nullptr;
    QDoubleSpinBox* m_offZ = nullptr;

    QPushButton*    m_removeBtn      = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
