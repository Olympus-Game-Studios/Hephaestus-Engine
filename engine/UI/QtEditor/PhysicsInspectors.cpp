/**
 * @file PhysicsInspectors.cpp
 * @brief Implementation of RigidBody, MeshCollider and PhysicsShape inspectors.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "PhysicsInspectors.h"
#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QStyle>
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
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setDecimals(decimals);
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

    m_staticCheck    = new QCheckBox(tr("Static Body"));
    m_triggerCheck   = new QCheckBox(tr("Trigger Volume"));
    m_gravityCheck   = new QCheckBox(tr("Use Gravity"));
    m_freezeRotCheck = new QCheckBox(tr("Freeze Rotation"));

    form->addRow(QString(), m_staticCheck);
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

    connect(m_staticCheck,    &QCheckBox::toggled, this, [this](bool) {
        updateDynamicFieldsVisibility();
        applyToComponent();
    });
    connect(m_triggerCheck,   &QCheckBox::toggled, this, [this](bool) {
        updateDynamicFieldsVisibility();
        applyToComponent();
    });
    connect(m_gravityCheck,   &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_freezeRotCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_massSpin,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linearDampSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angularDampSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_frictionSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_restitutionSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linVelX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linVelY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_linVelZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angVelX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angVelY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_angVelZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [onChange](double) { onChange(); });
    connect(m_debugDrawCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });

    // Presets
    connect(m_presetPlayerBtn, &QPushButton::clicked, this, [this]() { applyPreset(true); });
    connect(m_presetGroundBtn, &QPushButton::clicked, this, [this]() { applyPreset(false); });

    // Remove
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove RigidBody");
        if (reg.all_of<RigidBodyComponent>(m_entity)) reg.remove<RigidBodyComponent>(m_entity);
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "RigidBody");
    });
}

void RigidBodyInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<RigidBodyComponent>(m_entity)) return;

    const auto& rb = reg.get<RigidBodyComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    m_staticCheck->setChecked(rb.isStatic);
    m_triggerCheck->setChecked(rb.isTrigger);
    m_gravityCheck->setChecked(rb.useGravity);
    m_freezeRotCheck->setChecked(rb.freezeRotation);
    m_massSpin->setValue(static_cast<double>(rb.mass));
    m_linearDampSpin->setValue(static_cast<double>(rb.linearDamping));
    m_angularDampSpin->setValue(static_cast<double>(rb.angularDamping));
    m_frictionSpin->setValue(static_cast<double>(rb.friction));
    m_restitutionSpin->setValue(static_cast<double>(rb.restitution));
    m_linVelX->setValue(static_cast<double>(rb.linearVelocity.x));
    m_linVelY->setValue(static_cast<double>(rb.linearVelocity.y));
    m_linVelZ->setValue(static_cast<double>(rb.linearVelocity.z));
    m_angVelX->setValue(static_cast<double>(rb.angularVelocity.x));
    m_angVelY->setValue(static_cast<double>(rb.angularVelocity.y));
    m_angVelZ->setValue(static_cast<double>(rb.angularVelocity.z));
    m_debugDrawCheck->setChecked(rb.debugDraw);

    blockAllSignals(false);
    m_updating = false;

    updateDynamicFieldsVisibility();
}

void RigidBodyInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<RigidBodyComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Edit RigidBody");

    auto& rb = reg.get<RigidBodyComponent>(m_entity);
    rb.isStatic       = m_staticCheck->isChecked();
    rb.isTrigger      = m_triggerCheck->isChecked();
    rb.useGravity     = m_gravityCheck->isChecked();
    rb.freezeRotation = m_freezeRotCheck->isChecked();
    rb.mass           = static_cast<float>(m_massSpin->value());
    rb.linearDamping  = static_cast<float>(m_linearDampSpin->value());
    rb.angularDamping = static_cast<float>(m_angularDampSpin->value());
    rb.friction       = static_cast<float>(m_frictionSpin->value());
    rb.restitution    = static_cast<float>(m_restitutionSpin->value());
    rb.linearVelocity = Vec3(
        static_cast<float>(m_linVelX->value()),
        static_cast<float>(m_linVelY->value()),
        static_cast<float>(m_linVelZ->value()));
    rb.angularVelocity = Vec3(
        static_cast<float>(m_angVelX->value()),
        static_cast<float>(m_angVelY->value()),
        static_cast<float>(m_angVelZ->value()));
    rb.debugDraw = m_debugDrawCheck->isChecked();

    // Enforce static constraints (matches ImGui behavior)
    if (rb.isStatic || rb.isTrigger) {
        rb.mass = 0.0f;
        rb.useGravity = false;
    } else if (rb.mass <= 0.0f) {
        rb.mass = 1.0f;
    }

    m_ctx->MarkDirty();
}

void RigidBodyInspector::applyPreset(bool isPlayer)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<RigidBodyComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, isPlayer ? "Preset: Player" : "Preset: Ground");

    auto& rb = reg.get<RigidBodyComponent>(m_entity);

    if (isPlayer) {
        rb.isStatic = false; rb.isTrigger = false;
        rb.useGravity = true; rb.freezeRotation = true;
        rb.mass = 80.0f;
        rb.linearDamping = 0.1f; rb.angularDamping = 0.5f;
        rb.friction = 0.6f; rb.restitution = 0.0f;
    } else {
        rb.isStatic = true; rb.isTrigger = false;
        rb.useGravity = false; rb.freezeRotation = false;
        rb.mass = 0.0f;
        rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
        rb.friction = 0.5f; rb.restitution = 0.0f;
    }

    m_ctx->MarkDirty();
    Refresh();
}

void RigidBodyInspector::updateDynamicFieldsVisibility()
{
    bool isDynamic = !m_staticCheck->isChecked() && !m_triggerCheck->isChecked();
    m_massRow->setVisible(isDynamic);
    m_gravityCheck->setVisible(isDynamic);
}

void RigidBodyInspector::blockAllSignals(bool block)
{
    m_staticCheck->blockSignals(block);
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
    connect(m_enabledCheck,   &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_staticCheck,    &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_triggerCheck,   &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_complexCheck,   &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_debugDrawCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove MeshCollider");
        if (reg.all_of<MeshColliderComponent>(m_entity)) reg.remove<MeshColliderComponent>(m_entity);
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "MeshCollider");
    });
}

void MeshColliderInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshColliderComponent>(m_entity)) return;

    const auto& mc = reg.get<MeshColliderComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    m_enabledCheck->setChecked(mc.enabled);
    m_staticCheck->setChecked(mc.isStatic);
    m_triggerCheck->setChecked(mc.isTrigger);
    m_complexCheck->setChecked(mc.useComplexCollision);
    m_debugDrawCheck->setChecked(mc.debugDraw);
    m_contactsLabel->setText(tr("Contacts: %1").arg(mc.contacts.size()));

    blockAllSignals(false);
    m_updating = false;
}

void MeshColliderInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshColliderComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Edit MeshCollider");

    auto& mc = reg.get<MeshColliderComponent>(m_entity);
    mc.enabled             = m_enabledCheck->isChecked();
    mc.isStatic            = m_staticCheck->isChecked();
    mc.isTrigger           = m_triggerCheck->isChecked();
    mc.useComplexCollision = m_complexCheck->isChecked();
    mc.debugDraw           = m_debugDrawCheck->isChecked();

    m_ctx->MarkDirty();
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

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove PhysicsShape");
        if (reg.all_of<PhysicsShapeComponent>(m_entity)) reg.remove<PhysicsShapeComponent>(m_entity);
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "PhysicsShape");
    });
}

void PhysicsShapeInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<PhysicsShapeComponent>(m_entity)) return;

    const auto& ps = reg.get<PhysicsShapeComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    // Shape type
    int idx = 0;
    switch (ps.type) {
        case PhysicsShapeType::Box:     idx = 0; break;
        case PhysicsShapeType::Sphere:  idx = 1; break;
        case PhysicsShapeType::Capsule: idx = 2; break;
        default: idx = 0; break;
    }
    m_shapeCombo->setCurrentIndex(idx);

    m_extX->setValue(static_cast<double>(ps.extents.x));
    m_extY->setValue(static_cast<double>(ps.extents.y));
    m_extZ->setValue(static_cast<double>(ps.extents.z));
    m_radiusSpin->setValue(static_cast<double>(ps.radius));
    m_capHeightSpin->setValue(static_cast<double>(ps.height));
    m_capRadiusSpin->setValue(static_cast<double>(ps.radius));
    m_offX->setValue(static_cast<double>(ps.offset.x));
    m_offY->setValue(static_cast<double>(ps.offset.y));
    m_offZ->setValue(static_cast<double>(ps.offset.z));

    blockAllSignals(false);
    m_updating = false;

    updateShapeFieldsVisibility();
}

void PhysicsShapeInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<PhysicsShapeComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Edit PhysicsShape");

    auto& ps = reg.get<PhysicsShapeComponent>(m_entity);
    ps.type = static_cast<PhysicsShapeType>(m_shapeCombo->currentData().toInt());
    ps.extents = Vec3(
        static_cast<float>(m_extX->value()),
        static_cast<float>(m_extY->value()),
        static_cast<float>(m_extZ->value()));
    ps.radius = (ps.type == PhysicsShapeType::Capsule)
        ? static_cast<float>(m_capRadiusSpin->value())
        : static_cast<float>(m_radiusSpin->value());
    ps.height = static_cast<float>(m_capHeightSpin->value());
    ps.offset = Vec3(
        static_cast<float>(m_offX->value()),
        static_cast<float>(m_offY->value()),
        static_cast<float>(m_offZ->value()));

    m_ctx->MarkDirty();
}

void PhysicsShapeInspector::updateShapeFieldsVisibility()
{
    auto t = static_cast<PhysicsShapeType>(m_shapeCombo->currentData().toInt());
    m_boxGroup->setVisible(t == PhysicsShapeType::Box);
    m_sphereGroup->setVisible(t == PhysicsShapeType::Sphere);
    m_capsuleGroup->setVisible(t == PhysicsShapeType::Capsule);
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
