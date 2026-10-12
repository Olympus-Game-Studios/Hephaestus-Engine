#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterControllerInspector.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Physics/CharacterControllerSystem.h"
#include "../../Physics/PhysicsLayerConfig.h"
#include "../../Scene/CharacterControllerComponent.h"
#include "../../Scene/Components.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <filesystem>

namespace Hephaestus {
namespace QtEditor {
namespace {

QDoubleSpinBox* makeSpin(double minValue, double maxValue, double step)
{
    auto* spin = new QDoubleSpinBox();
    spin->setRange(minValue, maxValue);
    spin->setDecimals(3);
    spin->setSingleStep(step);
    return spin;
}

Physics::PhysicsLayerConfig LoadLayers()
{
    const std::string root = AssetManager::GetInstance().GetProjectRoot();
    if (root.empty()) {
        return Physics::PhysicsLayerConfig::Compatibility();
    }
    Physics::PhysicsLayerConfig config;
    Physics::PhysicsLayerConfig::LoadFromFile(
        Physics::PhysicsLayerConfig::ProjectFile(std::filesystem::path(root)), config);
    return config;
}

QString VecText(const Vec3& value)
{
    return QString("%1, %2, %3")
        .arg(value.x, 0, 'f', 2)
        .arg(value.y, 0, 'f', 2)
        .arg(value.z, 0, 'f', 2);
}

} // namespace

CharacterControllerInspector::CharacterControllerInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Character Controller"), ctx, entity, parent)
{
    SetTypeName("CharacterController");
    buildUI();
    connectSignals();
    Refresh();
    m_runtimeTimer.setInterval(200);
    connect(&m_runtimeTimer, &QTimer::timeout, this, [this]() { updateRuntimeReadout(); });
    m_runtimeTimer.start();
}

QDoubleSpinBox* CharacterControllerInspector::addSpin(QFormLayout* form, const QString& label, double min, double max, double step)
{
    QDoubleSpinBox* spin = makeSpin(min, max, step);
    form->addRow(label, spin);
    return spin;
}

void CharacterControllerInspector::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    auto addGroup = [&](const QString& title) {
        auto* box = new QGroupBox(title, this);
        auto* form = new QFormLayout(box);
        layout->addWidget(box);
        return form;
    };

    QFormLayout* shape = addGroup(tr("Shape"));
    m_enabled = new QCheckBox(tr("Enabled"));
    shape->addRow(QString(), m_enabled);
    m_height = addSpin(shape, tr("Height"), 0.05, 10.0, 0.05);
    m_radius = addSpin(shape, tr("Radius"), 0.05, 2.0, 0.01);
    m_crouchedHeight = addSpin(shape, tr("Crouched Height"), 0.05, 10.0, 0.05);

    QFormLayout* movement = addGroup(tr("Movement"));
    m_walkSpeed = addSpin(movement, tr("Walk Speed"), 0.0, 40.0, 0.1);
    m_acceleration = addSpin(movement, tr("Acceleration"), 0.0, 200.0, 1.0);
    m_deceleration = addSpin(movement, tr("Deceleration"), 0.0, 200.0, 1.0);
    m_airAcceleration = addSpin(movement, tr("Air Acceleration"), 0.0, 200.0, 1.0);
    m_airControl = addSpin(movement, tr("Air Control"), 0.0, 1.0, 0.05);
    m_jumpSpeed = addSpin(movement, tr("Jump Speed"), 0.0, 40.0, 0.1);
    m_gravityScale = addSpin(movement, tr("Gravity Scale"), -5.0, 5.0, 0.1);
    m_maxFallSpeed = addSpin(movement, tr("Max Fall Speed"), 0.0, 200.0, 1.0);

    QFormLayout* grounding = addGroup(tr("Grounding"));
    m_groundSnap = addSpin(grounding, tr("Ground Snap Distance"), 0.0, 5.0, 0.05);

    QFormLayout* slopes = addGroup(tr("Slopes / Steps"));
    m_maxSlope = addSpin(slopes, tr("Max Slope Angle"), 0.0, 89.0, 1.0);
    m_stepHeight = addSpin(slopes, tr("Step Height"), 0.0, 2.0, 0.05);

    QFormLayout* collision = addGroup(tr("Collision"));
    m_layer = new QComboBox();
    m_layerWarning = new QLabel();
    m_layerWarning->setWordWrap(true);
    collision->addRow(tr("Layer"), m_layer);
    collision->addRow(QString(), m_layerWarning);
    m_mass = addSpin(collision, tr("Mass"), 0.01, 1000.0, 1.0);
    m_pushStrength = addSpin(collision, tr("Push Strength"), 0.0, 10000.0, 10.0);

    QFormLayout* runtime = addGroup(tr("Runtime"));
    m_grounded = new QLabel(tr("—"));
    m_groundState = new QLabel(tr("—"));
    m_velocity = new QLabel(tr("—"));
    m_groundVelocity = new QLabel(tr("—"));
    m_support = new QLabel(tr("none"));
    m_crouched = new QLabel(tr("—"));
    m_standBlocked = new QLabel(tr("—"));
    runtime->addRow(tr("Grounded"), m_grounded);
    runtime->addRow(tr("Ground State"), m_groundState);
    runtime->addRow(tr("Velocity"), m_velocity);
    runtime->addRow(tr("Ground Velocity"), m_groundVelocity);
    runtime->addRow(tr("Supporting Entity"), m_support);
    runtime->addRow(tr("Crouched"), m_crouched);
    runtime->addRow(tr("Stand Blocked"), m_standBlocked);

    m_remove = new QPushButton(tr("Remove Component"));
    layout->addWidget(m_remove);
}

void CharacterControllerInspector::connectSignals()
{
    auto edited = [this]() {
        if (m_updating) return;
        BeginEdit("Edit Character Controller");
        applyToComponent();
        EndEdit();
    };
    connect(m_enabled, &QCheckBox::toggled, this, edited);
    const QDoubleSpinBox* spins[] = {
        m_height, m_radius, m_crouchedHeight, m_walkSpeed, m_acceleration, m_deceleration,
        m_airAcceleration, m_airControl, m_jumpSpeed, m_gravityScale, m_maxFallSpeed,
        m_groundSnap, m_maxSlope, m_stepHeight, m_mass, m_pushStrength
    };
    for (const QDoubleSpinBox* spin : spins) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, edited);
    }
    connect(m_layer, qOverload<int>(&QComboBox::currentIndexChanged), this, [edited](int) { edited(); });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        ForEachTarget<CharacterControllerComponent>([&](CharacterControllerComponent&, entt::entity entity) {
            if (m_ctx && m_ctx->HasActiveScene()) {
                Entity sceneEntity(entity, m_ctx->GetActiveScene());
                sceneEntity.RemoveComponent<CharacterControllerComponent>();
            }
        });
        emit componentRemoved(GetEntity(), QString::fromStdString(TypeName()));
    });
}

void CharacterControllerInspector::applyToComponent()
{
    if (m_updating) return;
    ForEachTarget<CharacterControllerComponent>([&](CharacterControllerComponent& component, entt::entity) {
        if (m_enabled->checkState() != Qt::PartiallyChecked) component.enabled = m_enabled->isChecked();
        double value = 0.0;
        if (TryReadSpin(m_height, value)) component.height = static_cast<float>(value);
        if (TryReadSpin(m_radius, value)) component.radius = static_cast<float>(value);
        if (TryReadSpin(m_crouchedHeight, value)) component.crouchedHeight = static_cast<float>(value);
        if (TryReadSpin(m_walkSpeed, value)) component.walkSpeed = static_cast<float>(value);
        if (TryReadSpin(m_acceleration, value)) component.acceleration = static_cast<float>(value);
        if (TryReadSpin(m_deceleration, value)) component.deceleration = static_cast<float>(value);
        if (TryReadSpin(m_airAcceleration, value)) component.airAcceleration = static_cast<float>(value);
        if (TryReadSpin(m_airControl, value)) component.airControl = static_cast<float>(value);
        if (TryReadSpin(m_jumpSpeed, value)) component.jumpSpeed = static_cast<float>(value);
        if (TryReadSpin(m_gravityScale, value)) component.gravityScale = static_cast<float>(value);
        if (TryReadSpin(m_maxFallSpeed, value)) component.maxFallSpeed = static_cast<float>(value);
        if (TryReadSpin(m_groundSnap, value)) component.groundSnapDistance = static_cast<float>(value);
        if (TryReadSpin(m_maxSlope, value)) component.maxSlopeAngleDegrees = static_cast<float>(value);
        if (TryReadSpin(m_stepHeight, value)) component.stepHeight = static_cast<float>(value);
        if (TryReadSpin(m_mass, value)) component.mass = static_cast<float>(value);
        if (TryReadSpin(m_pushStrength, value)) component.pushStrength = static_cast<float>(value);
        int layer = 0;
        if (TryReadCombo(m_layer, layer) && layer >= 0) {
            component.collisionLayer = static_cast<std::uint16_t>(layer);
        }
    });
    if (m_ctx) m_ctx->MarkDirty();
}

void CharacterControllerInspector::updateRuntimeReadout()
{
    Physics::CharacterControllerState state;
    bool found = false;
    if (m_ctx && m_ctx->CharacterControllers()) {
        VisitTargets<CharacterControllerComponent>([&](const CharacterControllerComponent&, entt::entity entity) {
            if (!found && m_ctx->CharacterControllers()->GetState(entity, state)) {
                found = true;
            }
        });
    }
    if (!found) {
        m_grounded->setText(tr("—"));
        m_groundState->setText(tr("—"));
        m_velocity->setText(tr("—"));
        m_groundVelocity->setText(tr("—"));
        m_support->setText(tr("none"));
        m_crouched->setText(tr("—"));
        m_standBlocked->setText(tr("—"));
        return;
    }
    m_grounded->setText(state.grounded ? tr("Yes") : tr("No"));
    m_groundState->setText(QString::fromUtf8(Physics::CharacterGroundStateName(state.groundState)));
    m_velocity->setText(VecText(state.velocity));
    m_groundVelocity->setText(VecText(state.groundVelocity));
    QString support = tr("none");
    if (state.supportingEntity != entt::null && m_ctx && m_ctx->HasActiveScene()) {
        const auto& registry = m_ctx->GetActiveScene()->GetRegistry();
        if (registry.valid(state.supportingEntity) && registry.all_of<TagComponent>(state.supportingEntity)) {
            support = QString::fromStdString(registry.get<TagComponent>(state.supportingEntity).tag);
        } else {
            support = tr("entity");
        }
    }
    m_support->setText(support);
    m_crouched->setText(state.crouched ? tr("Yes") : tr("No"));
    m_standBlocked->setText(state.standBlocked ? tr("Yes") : tr("No"));
}

void CharacterControllerInspector::Refresh()
{
    EndEdit();
    MixedBool enabled;
    MixedScalar height, radius, crouchedHeight, walkSpeed, acceleration, deceleration;
    MixedScalar airAcceleration, airControl, jumpSpeed, gravityScale, maxFallSpeed;
    MixedScalar groundSnap, maxSlope, stepHeight, mass, pushStrength, layer;
    VisitTargets<CharacterControllerComponent>([&](const CharacterControllerComponent& component, entt::entity) {
        AccumulateBool(enabled, component.enabled);
        AccumulateScalar(height, component.height);
        AccumulateScalar(radius, component.radius);
        AccumulateScalar(crouchedHeight, component.crouchedHeight);
        AccumulateScalar(walkSpeed, component.walkSpeed);
        AccumulateScalar(acceleration, component.acceleration);
        AccumulateScalar(deceleration, component.deceleration);
        AccumulateScalar(airAcceleration, component.airAcceleration);
        AccumulateScalar(airControl, component.airControl);
        AccumulateScalar(jumpSpeed, component.jumpSpeed);
        AccumulateScalar(gravityScale, component.gravityScale);
        AccumulateScalar(maxFallSpeed, component.maxFallSpeed);
        AccumulateScalar(groundSnap, component.groundSnapDistance);
        AccumulateScalar(maxSlope, component.maxSlopeAngleDegrees);
        AccumulateScalar(stepHeight, component.stepHeight);
        AccumulateScalar(mass, component.mass);
        AccumulateScalar(pushStrength, component.pushStrength);
        AccumulateScalar(layer, component.collisionLayer);
    });
    if (!height.present) return;

    m_updating = true;
    const Physics::PhysicsLayerConfig layers = LoadLayers();
    {
        const QSignalBlocker blocker(m_layer);
        m_layer->clear();
        for (const Physics::PhysicsLayerDesc& desc : layers.Layers()) {
            m_layer->addItem(QString::fromStdString(desc.name), static_cast<int>(desc.id));
        }
        if (m_layer->count() == 0) {
            m_layer->addItem(tr("Default"), 0);
        }
    }
    m_layerWarning->clear();
    m_layerWarning->setVisible(false);
    if (layer.present && !layer.mixed) {
        const auto layerId = static_cast<std::uint16_t>(layer.value);
        if (!layers.HasLayer(layerId) || layers.IsTombstoned(layerId)) {
            m_layer->addItem(tr("Unknown layer %1").arg(layerId), static_cast<int>(layerId));
            m_layerWarning->setText(tr("This layer is resolved to Default on Play until another layer is chosen."));
            m_layerWarning->setVisible(true);
        }
        SetComboFromMixed(m_layer, false, static_cast<int>(layerId));
    } else if (layer.mixed) {
        SetComboFromMixed(m_layer, true, 0);
    }
    SetCheckFromMixed(m_enabled, enabled);
    SetSpinFromMixed(m_height, height);
    SetSpinFromMixed(m_radius, radius);
    SetSpinFromMixed(m_crouchedHeight, crouchedHeight);
    SetSpinFromMixed(m_walkSpeed, walkSpeed);
    SetSpinFromMixed(m_acceleration, acceleration);
    SetSpinFromMixed(m_deceleration, deceleration);
    SetSpinFromMixed(m_airAcceleration, airAcceleration);
    SetSpinFromMixed(m_airControl, airControl);
    SetSpinFromMixed(m_jumpSpeed, jumpSpeed);
    SetSpinFromMixed(m_gravityScale, gravityScale);
    SetSpinFromMixed(m_maxFallSpeed, maxFallSpeed);
    SetSpinFromMixed(m_groundSnap, groundSnap);
    SetSpinFromMixed(m_maxSlope, maxSlope);
    SetSpinFromMixed(m_stepHeight, stepHeight);
    SetSpinFromMixed(m_mass, mass);
    SetSpinFromMixed(m_pushStrength, pushStrength);
    m_updating = false;
    updateRuntimeReadout();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
