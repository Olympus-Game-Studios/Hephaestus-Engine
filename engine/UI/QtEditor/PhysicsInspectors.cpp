/**
 * @file PhysicsInspectors.cpp
 * @brief Implementation of RigidBody, MeshCollider and PhysicsShape inspectors.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "PhysicsInspectors.h"
#include "EditorUndoCommand.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Core/EditorContext.h"
#include "../../Physics/PhysicsLayerConfig.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QSignalBlocker>
#include <QStyle>

#include <filesystem>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ───────────────────────────────── helpers ────────────────────────────────

static QPushButton* makeResetBtn(QWidget* parent)
{
    auto* btn = new QPushButton(parent);
    btn->setIcon(btn->style()->standardIcon(QStyle::SP_BrowserReload));
    btn->setFixedSize(20, 20);
    btn->setToolTip("Reset to default");
    btn->setFlat(true);
    return btn;
}

static QDoubleSpinBox* makeDoubleSpin(double min, double max, double step, int decimals)
{
    auto* sb = new QDoubleSpinBox();
    ConfigureMixedSpin(sb, min, max, step, decimals);
    sb->setMinimumWidth(60);
    return sb;
}

// helper to create vec3 row
static QWidget* makeVec3Row(QDoubleSpinBox*& x, QDoubleSpinBox*& y, QDoubleSpinBox*& z,
                            double min, double max, double step, int decs)
{
    auto* w = new QWidget();
    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(4);
    x = makeDoubleSpin(min, max, step, decs);
    y = makeDoubleSpin(min, max, step, decs);
    z = makeDoubleSpin(min, max, step, decs);
    hl->addWidget(x);
    hl->addWidget(y);
    hl->addWidget(z);
    return w;
}

// ═════════════════════════════════════════════════════════════════════════════
// RigidBodyInspector
// ═════════════════════════════════════════════════════════════════════════════

RigidBodyInspector::RigidBodyInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Rigid Body"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

void RigidBodyInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    // Presets
    {
        auto* row = new QHBoxLayout();
        m_presetPlayerBtn = new QPushButton(tr("Preset: Player"));
        m_presetGroundBtn = new QPushButton(tr("Preset: Ground/Env"));
        row->addWidget(m_presetPlayerBtn);
        row->addWidget(m_presetGroundBtn);
        mainLayout->addLayout(row);
    }

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_motionCombo = new QComboBox();
    m_motionCombo->addItem(tr("Dynamic"), static_cast<int>(RigidBodyMotion::Dynamic));
    m_motionCombo->addItem(tr("Static"), static_cast<int>(RigidBodyMotion::Static));
    m_motionCombo->addItem(tr("Kinematic"), static_cast<int>(RigidBodyMotion::Kinematic));
    m_layerCombo = new QComboBox();
    m_layerWarning = new QLabel();
    m_layerWarning->setWordWrap(true);
    m_layerWarning->setStyleSheet("color: #b8860b;");
    m_layerWarning->setVisible(false);
    m_triggerCheck   = new QCheckBox(tr("Trigger Volume"));
    m_gravityCheck   = new QCheckBox(tr("Use Gravity"));
    m_freezeRotCheck = new QCheckBox(tr("Freeze Rotation"));

    form->addRow(tr("Motion:"), m_motionCombo);
    form->addRow(tr("Layer:"), m_layerCombo);
    form->addRow(QString(), m_layerWarning);
    form->addRow(QString(), m_triggerCheck);
    form->addRow(QString(), m_gravityCheck);
    form->addRow(QString(), m_freezeRotCheck);

    // Mass
    {
        m_massRow = new QWidget();
        auto* row = new QHBoxLayout(m_massRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_massSpin = makeDoubleSpin(0.01, 1000.0, 0.1, 2);
        m_massSpin->setSuffix(" kg");
        row->addWidget(m_massSpin, 1);
        form->addRow(tr("Mass:"), m_massRow);
    }

    m_linearDampSpin  = makeDoubleSpin(0.0, 10.0, 0.01, 3);
    m_angularDampSpin = makeDoubleSpin(0.0, 10.0, 0.01, 3);
    m_frictionSpin    = makeDoubleSpin(0.0, 2.0, 0.05, 3);
    m_restitutionSpin = makeDoubleSpin(0.0, 1.0, 0.05, 3);

    form->addRow(tr("Linear Damping:"),  m_linearDampSpin);
    form->addRow(tr("Angular Damping:"), m_angularDampSpin);
    form->addRow(tr("Friction:"),        m_frictionSpin);
    form->addRow(tr("Restitution:"),     m_restitutionSpin);

    // Velocities
    auto* linVelRow = makeVec3Row(m_linVelX, m_linVelY, m_linVelZ, -1000, 1000, 0.1, 3);
    form->addRow(tr("Linear Vel:"), linVelRow);

    auto* angVelRow = makeVec3Row(m_angVelX, m_angVelY, m_angVelZ, -1000, 1000, 0.1, 3);
    form->addRow(tr("Angular Vel:"), angVelRow);

    m_debugDrawCheck = new QCheckBox(tr("Debug Draw Collider"));
    form->addRow(QString(), m_debugDrawCheck);

    mainLayout->addLayout(form);

    m_removeBtn = new QPushButton(tr("Remove Rigid Body"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeBtn);
}

void RigidBodyInspector::connectSignals()
{
    auto onChange = [this]() { applyToComponent(); };

    connect(m_motionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateDynamicFieldsVisibility();
        applyToComponent();
        EndEdit();
    });
    connect(m_layerCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        applyToComponent();
        EndEdit();
    });
    connect(m_triggerCheck,   &QCheckBox::toggled, this, [this](bool) {
        updateDynamicFieldsVisibility();
        applyToComponent();
        EndEdit();
    });
    connect(m_gravityCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_freezeRotCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_massSpin,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_massSpin,      &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_linearDampSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linearDampSpin,&QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_angularDampSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angularDampSpin,&QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_frictionSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_frictionSpin,  &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_restitutionSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_restitutionSpin,&QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_linVelX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linVelY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linVelZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angVelX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angVelY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angVelZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    for (auto* spin : {m_linVelX, m_linVelY, m_linVelZ, m_angVelX, m_angVelY, m_angVelZ}) {
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    }
    connect(m_debugDrawCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });

    // Presets
    connect(m_presetPlayerBtn, &QPushButton::clicked, this, [this]() { applyPreset(true); });
    connect(m_presetGroundBtn, &QPushButton::clicked, this, [this]() { applyPreset(false); });

    // Remove
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove RigidBody");
        ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent&, entt::entity entity) {
            if (reg.all_of<RigidBodyComponent>(entity)) {
                reg.remove<RigidBodyComponent>(entity);
            }
        });
        emit componentRemoved(m_entity, "RigidBody");
    });
}

namespace {

Physics::PhysicsLayerConfig LoadProjectLayers()
{
    const std::string root = AssetManager::GetInstance().GetProjectRoot();
    Physics::PhysicsLayerConfig config;
    if (root.empty()) {
        return Physics::PhysicsLayerConfig::Compatibility();
    }
    Physics::PhysicsLayerConfig::LoadFromFile(
        Physics::PhysicsLayerConfig::ProjectFile(std::filesystem::path(root)), config);
    return config;
}

} // namespace

void RigidBodyInspector::Refresh()
{
    EndEdit();
    MixedBool isTrigger, useGravity, freezeRot, debugDraw;
    MixedScalar layer;
    int motionIndex = -1;
    bool motionMixed = false;
    MixedScalar mass, linDamp, angDamp, friction, restitution;
    MixedScalar linX, linY, linZ, angX, angY, angZ;
    VisitTargets<RigidBodyComponent>([&](const RigidBodyComponent& rb, entt::entity) {
        AccumulateScalar(layer, static_cast<float>(rb.layer));
        const int index = static_cast<int>(rb.motion);
        if (motionIndex < 0) {
            motionIndex = index;
        } else if (motionIndex != index) {
            motionMixed = true;
        }
        AccumulateBool(isTrigger, rb.isTrigger);
        AccumulateBool(useGravity, rb.useGravity);
        AccumulateBool(freezeRot, rb.freezeRotation);
        AccumulateBool(debugDraw, rb.debugDraw);
        AccumulateScalar(mass, rb.mass);
        AccumulateScalar(linDamp, rb.linearDamping);
        AccumulateScalar(angDamp, rb.angularDamping);
        AccumulateScalar(friction, rb.friction);
        AccumulateScalar(restitution, rb.restitution);
        AccumulateScalar(linX, rb.linearVelocity.x);
        AccumulateScalar(linY, rb.linearVelocity.y);
        AccumulateScalar(linZ, rb.linearVelocity.z);
        AccumulateScalar(angX, rb.angularVelocity.x);
        AccumulateScalar(angY, rb.angularVelocity.y);
        AccumulateScalar(angZ, rb.angularVelocity.z);
    });
    if (!mass.present) return;

    m_updating = true;
    blockAllSignals(true);
    if (!motionMixed && motionIndex >= 0) {
        const int comboIndex = m_motionCombo->findData(motionIndex);
        if (comboIndex >= 0) {
            m_motionCombo->setCurrentIndex(comboIndex);
        }
    }
    const Physics::PhysicsLayerConfig layers = LoadProjectLayers();
    {
        const QSignalBlocker blocker(m_layerCombo);
        m_layerCombo->clear();
        for (const Physics::PhysicsLayerDesc& desc : layers.Layers()) {
            m_layerCombo->addItem(QString::fromStdString(desc.name), static_cast<int>(desc.id));
        }
        if (m_layerCombo->count() == 0) {
            m_layerCombo->addItem(tr("Default"), 0);
        }
    }
    m_layerWarning->setVisible(false);
    m_layerWarning->clear();
    if (layer.present && !layer.mixed) {
        const auto layerId = static_cast<std::uint16_t>(layer.value);
        if (!layers.HasLayer(layerId) || layers.IsTombstoned(layerId)) {
            const QString label = layers.IsTombstoned(layerId)
                ? tr("Deleted layer %1").arg(layerId)
                : tr("Unknown layer %1").arg(layerId);
            m_layerCombo->addItem(label, static_cast<int>(layerId));
            m_layerWarning->setText(layers.IsTombstoned(layerId)
                ? tr("This layer was deleted. Play resolves it to Default until another layer is chosen.")
                : tr("This layer is not in the project file. Play resolves it to Default until another layer is chosen."));
            m_layerWarning->setVisible(true);
        }
        SetComboFromMixed(m_layerCombo, false, static_cast<int>(layerId));
    } else if (layer.mixed) {
        SetComboFromMixed(m_layerCombo, true, 0);
    }
    SetCheckFromMixed(m_triggerCheck, isTrigger);
    SetCheckFromMixed(m_gravityCheck, useGravity);
    SetCheckFromMixed(m_freezeRotCheck, freezeRot);
    SetSpinFromMixed(m_massSpin, mass);
    SetSpinFromMixed(m_linearDampSpin, linDamp);
    SetSpinFromMixed(m_angularDampSpin, angDamp);
    SetSpinFromMixed(m_frictionSpin, friction);
    SetSpinFromMixed(m_restitutionSpin, restitution);
    SetSpinFromMixed(m_linVelX, linX);
    SetSpinFromMixed(m_linVelY, linY);
    SetSpinFromMixed(m_linVelZ, linZ);
    SetSpinFromMixed(m_angVelX, angX);
    SetSpinFromMixed(m_angVelY, angY);
    SetSpinFromMixed(m_angVelZ, angZ);
    SetCheckFromMixed(m_debugDrawCheck, debugDraw);
    blockAllSignals(false);
    m_updating = false;
    updateDynamicFieldsVisibility();
}

void RigidBodyInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Edit RigidBody");

    auto writeBool = [&](QCheckBox* box, bool RigidBodyComponent::* field) {
        bool value = false;
        if (!TryReadCheck(box, value)) return;
        ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent& rb, entt::entity) {
            rb.*field = value;
        });
    };
    int layerValue = 0;
    if (TryReadCombo(m_layerCombo, layerValue) && layerValue >= 0) {
        ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent& rb, entt::entity) {
            rb.layer = static_cast<std::uint16_t>(layerValue);
        });
    }
    writeBool(m_triggerCheck, &RigidBodyComponent::isTrigger);
    writeBool(m_gravityCheck, &RigidBodyComponent::useGravity);
    writeBool(m_freezeRotCheck, &RigidBodyComponent::freezeRotation);
    writeBool(m_debugDrawCheck, &RigidBodyComponent::debugDraw);

    auto writeScalar = [&](QDoubleSpinBox* spin, float RigidBodyComponent::* field) {
        double value = 0.0;
        if (!TryReadSpin(spin, value)) return;
        ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent& rb, entt::entity) {
            rb.*field = static_cast<float>(value);
        });
    };
    writeScalar(m_massSpin, &RigidBodyComponent::mass);
    writeScalar(m_linearDampSpin, &RigidBodyComponent::linearDamping);
    writeScalar(m_angularDampSpin, &RigidBodyComponent::angularDamping);
    writeScalar(m_frictionSpin, &RigidBodyComponent::friction);
    writeScalar(m_restitutionSpin, &RigidBodyComponent::restitution);

    auto writeAxis = [&](QDoubleSpinBox* spin, Vec3 RigidBodyComponent::* field, int axis) {
        double value = 0.0;
        if (!TryReadSpin(spin, value)) return;
        ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent& rb, entt::entity) {
            WriteVec3Axis(rb.*field, axis, static_cast<float>(value));
        });
    };
    writeAxis(m_linVelX, &RigidBodyComponent::linearVelocity, 0);
    writeAxis(m_linVelY, &RigidBodyComponent::linearVelocity, 1);
    writeAxis(m_linVelZ, &RigidBodyComponent::linearVelocity, 2);
    writeAxis(m_angVelX, &RigidBodyComponent::angularVelocity, 0);
    writeAxis(m_angVelY, &RigidBodyComponent::angularVelocity, 1);
    writeAxis(m_angVelZ, &RigidBodyComponent::angularVelocity, 2);

    int motionValue = 0;
    if (TryReadCombo(m_motionCombo, motionValue)) {
        ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent& rb, entt::entity) {
            rb.SetMotion(static_cast<RigidBodyMotion>(motionValue));
        });
    }
}

void RigidBodyInspector::applyPreset(bool isPlayer)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    EditorUndoManager::PushSnapshot(m_ctx, isPlayer ? "Preset: Player" : "Preset: Ground");
    ForEachTarget<RigidBodyComponent>([&](RigidBodyComponent& rb, entt::entity) {
        if (isPlayer) {
            rb.isTrigger = false;
            rb.freezeRotation = true;
            rb.mass = 80.0f;
            rb.SetMotion(RigidBodyMotion::Dynamic);
            rb.useGravity = true;
            rb.linearDamping = 0.1f; rb.angularDamping = 0.5f;
            rb.friction = 0.6f; rb.restitution = 0.0f;
        } else {
            rb.isTrigger = false;
            rb.freezeRotation = false;
            rb.SetMotion(RigidBodyMotion::Static);
            rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
            rb.friction = 0.5f; rb.restitution = 0.0f;
        }
    });
    Refresh();
}

void RigidBodyInspector::updateDynamicFieldsVisibility()
{
    const bool isDynamic = m_motionCombo->currentData().toInt() == static_cast<int>(RigidBodyMotion::Dynamic);
    const bool kinematic = m_motionCombo->currentData().toInt() == static_cast<int>(RigidBodyMotion::Kinematic);
    m_massRow->setVisible(isDynamic);
    m_gravityCheck->setVisible(isDynamic);
    if (m_linVelX && m_linVelX->parentWidget()) {
        m_linVelX->parentWidget()->setVisible(!kinematic);
    }
    if (m_angVelX && m_angVelX->parentWidget()) {
        m_angVelX->parentWidget()->setVisible(!kinematic);
    }
}

void RigidBodyInspector::blockAllSignals(bool block)
{
    m_motionCombo->blockSignals(block);
    m_layerCombo->blockSignals(block);
    m_triggerCheck->blockSignals(block);
    m_gravityCheck->blockSignals(block);
    m_freezeRotCheck->blockSignals(block);
    m_massSpin->blockSignals(block);
    m_linearDampSpin->blockSignals(block);
    m_angularDampSpin->blockSignals(block);
    m_frictionSpin->blockSignals(block);
    m_restitutionSpin->blockSignals(block);
    m_linVelX->blockSignals(block);
    m_linVelY->blockSignals(block);
    m_linVelZ->blockSignals(block);
    m_angVelX->blockSignals(block);
    m_angVelY->blockSignals(block);
    m_angVelZ->blockSignals(block);
    m_debugDrawCheck->blockSignals(block);
}

// ═════════════════════════════════════════════════════════════════════════════
// MeshColliderInspector
// ═════════════════════════════════════════════════════════════════════════════

MeshColliderInspector::MeshColliderInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Mesh Collider"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

void MeshColliderInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_enabledCheck   = new QCheckBox(tr("Collision Enabled"));
    m_staticCheck    = new QCheckBox(tr("Static Collider"));
    m_triggerCheck   = new QCheckBox(tr("Trigger"));
    m_complexCheck   = new QCheckBox(tr("Complex Collision"));
    m_debugDrawCheck = new QCheckBox(tr("Debug Draw Bounds"));

    form->addRow(QString(), m_enabledCheck);
    form->addRow(QString(), m_staticCheck);
    form->addRow(QString(), m_triggerCheck);
    form->addRow(QString(), m_complexCheck);
    form->addRow(QString(), m_debugDrawCheck);

    // Contacts display (read only)
    m_contactsLabel = new QLabel(tr("Contacts: 0"));
    m_contactsLabel->setStyleSheet("color: gray; font-size: 10px;");
    form->addRow(QString(), m_contactsLabel);

    mainLayout->addLayout(form);

    m_removeBtn = new QPushButton(tr("Remove Mesh Collider"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeBtn);
}

void MeshColliderInspector::connectSignals()
{
    auto applyBool = [this](bool) {
        applyToComponent();
        EndEdit();
    };
    connect(m_enabledCheck,   &QCheckBox::toggled, this, applyBool);
    connect(m_staticCheck,    &QCheckBox::toggled, this, applyBool);
    connect(m_triggerCheck,   &QCheckBox::toggled, this, applyBool);
    connect(m_complexCheck,   &QCheckBox::toggled, this, applyBool);
    connect(m_debugDrawCheck, &QCheckBox::toggled, this, applyBool);

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove MeshCollider");
        ForEachTarget<MeshColliderComponent>([&](MeshColliderComponent&, entt::entity entity) {
            if (reg.all_of<MeshColliderComponent>(entity)) {
                reg.remove<MeshColliderComponent>(entity);
            }
        });
        emit componentRemoved(m_entity, "MeshCollider");
    });
}

void MeshColliderInspector::Refresh()
{
    EndEdit();
    MixedBool enabled, isStatic, isTrigger, complex, debugDraw;
    int contacts = 0;
    VisitTargets<MeshColliderComponent>([&](const MeshColliderComponent& mc, entt::entity) {
        AccumulateBool(enabled, mc.enabled);
        AccumulateBool(isStatic, mc.isStatic);
        AccumulateBool(isTrigger, mc.isTrigger);
        AccumulateBool(complex, mc.useComplexCollision);
        AccumulateBool(debugDraw, mc.debugDraw);
        contacts = static_cast<int>(mc.contacts.size());
    });
    if (!enabled.present) return;
    m_updating = true;
    blockAllSignals(true);
    SetCheckFromMixed(m_enabledCheck, enabled);
    SetCheckFromMixed(m_staticCheck, isStatic);
    SetCheckFromMixed(m_triggerCheck, isTrigger);
    SetCheckFromMixed(m_complexCheck, complex);
    SetCheckFromMixed(m_debugDrawCheck, debugDraw);
    m_contactsLabel->setText(tr("Contacts: %1").arg(contacts));
    blockAllSignals(false);
    m_updating = false;
}

void MeshColliderInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Edit MeshCollider");
    auto writeBool = [&](QCheckBox* box, bool MeshColliderComponent::* field) {
        bool value = false;
        if (!TryReadCheck(box, value)) return;
        ForEachTarget<MeshColliderComponent>([&](MeshColliderComponent& mc, entt::entity) {
            mc.*field = value;
        });
    };
    writeBool(m_enabledCheck, &MeshColliderComponent::enabled);
    writeBool(m_staticCheck, &MeshColliderComponent::isStatic);
    writeBool(m_triggerCheck, &MeshColliderComponent::isTrigger);
    writeBool(m_complexCheck, &MeshColliderComponent::useComplexCollision);
    writeBool(m_debugDrawCheck, &MeshColliderComponent::debugDraw);
}

void MeshColliderInspector::blockAllSignals(bool block)
{
    m_enabledCheck->blockSignals(block);
    m_staticCheck->blockSignals(block);
    m_triggerCheck->blockSignals(block);
    m_complexCheck->blockSignals(block);
    m_debugDrawCheck->blockSignals(block);
}

// ═════════════════════════════════════════════════════════════════════════════
// PhysicsShapeInspector
// ═════════════════════════════════════════════════════════════════════════════

PhysicsShapeInspector::PhysicsShapeInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Physics Shape"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

void PhysicsShapeInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Shape type combo
    m_shapeCombo = new QComboBox();
    m_shapeCombo->addItem(tr("Box"),     static_cast<int>(PhysicsShapeType::Box));
    m_shapeCombo->addItem(tr("Sphere"),  static_cast<int>(PhysicsShapeType::Sphere));
    m_shapeCombo->addItem(tr("Capsule"), static_cast<int>(PhysicsShapeType::Capsule));
    m_shapeCombo->addItem(tr("Cylinder"), static_cast<int>(PhysicsShapeType::Cylinder));
    form->addRow(tr("Shape:"), m_shapeCombo);

    // Box extents
    m_boxGroup = makeVec3Row(m_extX, m_extY, m_extZ, 0.01, 100, 0.05, 3);
    form->addRow(tr("Half Extents:"), m_boxGroup);

    // Sphere radius
    m_sphereGroup = new QWidget();
    {
        auto* hl = new QHBoxLayout(m_sphereGroup);
        hl->setContentsMargins(0, 0, 0, 0);
        m_radiusSpin = makeDoubleSpin(0.01, 100, 0.05, 3);
        hl->addWidget(m_radiusSpin, 1);
    }
    form->addRow(tr("Radius:"), m_sphereGroup);

    // Capsule
    m_capsuleGroup = new QWidget();
    {
        auto* hl = new QHBoxLayout(m_capsuleGroup);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        m_capHeightSpin = makeDoubleSpin(0.01, 100, 0.05, 3);
        m_capRadiusSpin = makeDoubleSpin(0.01, 100, 0.05, 3);
        hl->addWidget(new QLabel(tr("H:")));
        hl->addWidget(m_capHeightSpin);
        hl->addWidget(new QLabel(tr("R:")));
        hl->addWidget(m_capRadiusSpin);
    }
    form->addRow(tr("Capsule:"), m_capsuleGroup);

    // Offset
    auto* offRow = makeVec3Row(m_offX, m_offY, m_offZ, -100, 100, 0.05, 3);
    form->addRow(tr("Offset:"), offRow);

    mainLayout->addLayout(form);

    m_removeBtn = new QPushButton(tr("Remove Physics Shape"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeBtn);
}

void PhysicsShapeInspector::connectSignals()
{
    connect(m_shapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        auto newType = static_cast<PhysicsShapeType>(m_shapeCombo->currentData().toInt());
        autoFitToMeshBounds(newType);
        updateShapeFieldsVisibility();
        applyToComponent();
        EndEdit();
    });

    auto onChange = [this]() { applyToComponent(); };
    connect(m_extX,          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_extY,          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_extZ,          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_radiusSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_capHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_capRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_offX,          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_offY,          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_offZ,          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    for (auto* spin : {m_extX, m_extY, m_extZ, m_radiusSpin, m_capHeightSpin,
                       m_capRadiusSpin, m_offX, m_offY, m_offZ}) {
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    }

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove PhysicsShape");
        ForEachTarget<PhysicsShapeComponent>([&](PhysicsShapeComponent&, entt::entity entity) {
            if (reg.all_of<PhysicsShapeComponent>(entity)) {
                reg.remove<PhysicsShapeComponent>(entity);
            }
        });
        emit componentRemoved(m_entity, "PhysicsShape");
    });
}

void PhysicsShapeInspector::Refresh()
{
    EndEdit();
    MixedScalar extX, extY, extZ, radius, height, offX, offY, offZ;
    int firstType = -1;
    bool mixedType = false;
    VisitTargets<PhysicsShapeComponent>([&](const PhysicsShapeComponent& ps, entt::entity) {
        AccumulateScalar(extX, ps.extents.x);
        AccumulateScalar(extY, ps.extents.y);
        AccumulateScalar(extZ, ps.extents.z);
        AccumulateScalar(radius, ps.radius);
        AccumulateScalar(height, ps.height);
        AccumulateScalar(offX, ps.offset.x);
        AccumulateScalar(offY, ps.offset.y);
        AccumulateScalar(offZ, ps.offset.z);
        const int t = static_cast<int>(ps.type);
        if (firstType < 0) firstType = t;
        else if (firstType != t) mixedType = true;
    });
    if (!extX.present) return;

    m_updating = true;
    blockAllSignals(true);
    SetComboFromMixed(m_shapeCombo, mixedType, firstType);
    SetSpinFromMixed(m_extX, extX);
    SetSpinFromMixed(m_extY, extY);
    SetSpinFromMixed(m_extZ, extZ);
    SetSpinFromMixed(m_radiusSpin, radius);
    SetSpinFromMixed(m_capHeightSpin, height);
    SetSpinFromMixed(m_capRadiusSpin, radius);
    SetSpinFromMixed(m_offX, offX);
    SetSpinFromMixed(m_offY, offY);
    SetSpinFromMixed(m_offZ, offZ);
    blockAllSignals(false);
    m_updating = false;
    updateShapeFieldsVisibility();
}

void PhysicsShapeInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Edit PhysicsShape");
    int type = 0;
    if (TryReadCombo(m_shapeCombo, type)) {
        ForEachTarget<PhysicsShapeComponent>([&](PhysicsShapeComponent& ps, entt::entity) {
            ps.type = static_cast<PhysicsShapeType>(type);
        });
    }
    auto writeExtent = [&](QDoubleSpinBox* spin, int axis) {
        double value = 0.0;
        if (!TryReadSpin(spin, value)) return;
        ForEachTarget<PhysicsShapeComponent>([&](PhysicsShapeComponent& ps, entt::entity) {
            WriteVec3Axis(ps.extents, axis, static_cast<float>(value));
        });
    };
    writeExtent(m_extX, 0);
    writeExtent(m_extY, 1);
    writeExtent(m_extZ, 2);
    double radius = 0.0, height = 0.0;
    const int shapeType = m_shapeCombo->currentData().toInt();
    const bool editingCapsule = shapeType == static_cast<int>(PhysicsShapeType::Capsule)
        || shapeType == static_cast<int>(PhysicsShapeType::Cylinder);
    QDoubleSpinBox* radiusEditor = editingCapsule ? m_capRadiusSpin : m_radiusSpin;
    if (TryReadSpin(radiusEditor, radius)) {
        ForEachTarget<PhysicsShapeComponent>([&](PhysicsShapeComponent& ps, entt::entity) {
            ps.radius = static_cast<float>(radius);
        });
    }
    if (TryReadSpin(m_capHeightSpin, height)) {
        ForEachTarget<PhysicsShapeComponent>([&](PhysicsShapeComponent& ps, entt::entity) {
            ps.height = static_cast<float>(height);
        });
    }
    auto writeOff = [&](QDoubleSpinBox* spin, int axis) {
        double value = 0.0;
        if (!TryReadSpin(spin, value)) return;
        ForEachTarget<PhysicsShapeComponent>([&](PhysicsShapeComponent& ps, entt::entity) {
            WriteVec3Axis(ps.offset, axis, static_cast<float>(value));
        });
    };
    writeOff(m_offX, 0);
    writeOff(m_offY, 1);
    writeOff(m_offZ, 2);
}

void PhysicsShapeInspector::updateShapeFieldsVisibility()
{
    auto t = static_cast<PhysicsShapeType>(m_shapeCombo->currentData().toInt());
    m_boxGroup->setVisible(t == PhysicsShapeType::Box);
    m_sphereGroup->setVisible(t == PhysicsShapeType::Sphere);
    m_capsuleGroup->setVisible(t == PhysicsShapeType::Capsule || t == PhysicsShapeType::Cylinder);
}

void PhysicsShapeInspector::autoFitToMeshBounds(PhysicsShapeType newType)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    const auto& renderer = reg.get<MeshRendererComponent>(m_entity);
    Vec3 mn = renderer.localBounds.min;
    Vec3 mx = renderer.localBounds.max;
    Vec3 half((mx.x - mn.x) * 0.5f,
              (mx.y - mn.y) * 0.5f,
              (mx.z - mn.z) * 0.5f);
    Vec3 center((mn.x + mx.x) * 0.5f,
                (mn.y + mx.y) * 0.5f,
                (mn.z + mx.z) * 0.5f);

    blockAllSignals(true);

    // The physics system automatically offsets the shape by the mesh bounds center,
    // so the user offset should default to zero (= centered on mesh geometry).
    m_offX->setValue(0.0);
    m_offY->setValue(0.0);
    m_offZ->setValue(0.0);

    switch (newType) {
        case PhysicsShapeType::Box: {
            m_extX->setValue(static_cast<double>(std::max(half.x, 0.001f)));
            m_extY->setValue(static_cast<double>(std::max(half.y, 0.001f)));
            m_extZ->setValue(static_cast<double>(std::max(half.z, 0.001f)));
            break;
        }
        case PhysicsShapeType::Sphere: {
            float maxHalf = std::max(half.x, std::max(half.y, half.z));
            m_radiusSpin->setValue(static_cast<double>(std::max(maxHalf, 0.001f)));
            break;
        }
        case PhysicsShapeType::Cylinder:
        case PhysicsShapeType::Capsule: {
            // Capsule along Y axis.  Jolt total height = 2 * halfHeight + 2 * radius.
            // radius = max(halfX, halfZ), height (cylinder part) = max(2*(halfY - radius), 0.01)
            float capRadius = std::max(half.x, half.z);
            float cylHeight = std::max((half.y - capRadius) * 2.0f, 0.01f);
            m_capRadiusSpin->setValue(static_cast<double>(std::max(capRadius, 0.001f)));
            m_capHeightSpin->setValue(static_cast<double>(cylHeight));
            break;
        }
        default:
            break;
    }

    blockAllSignals(false);
}

void PhysicsShapeInspector::blockAllSignals(bool block)
{
    m_shapeCombo->blockSignals(block);
    m_extX->blockSignals(block);
    m_extY->blockSignals(block);
    m_extZ->blockSignals(block);
    m_radiusSpin->blockSignals(block);
    m_capHeightSpin->blockSignals(block);
    m_capRadiusSpin->blockSignals(block);
    m_offX->blockSignals(block);
    m_offY->blockSignals(block);
    m_offZ->blockSignals(block);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
