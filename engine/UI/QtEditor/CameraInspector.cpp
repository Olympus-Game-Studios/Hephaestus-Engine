/**
 * @file CameraInspector.cpp
 * @brief Implementation of the CameraComponent inspector.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "CameraInspector.h"
#include "EditorUndoCommand.h"
#include "PropertyEditors.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QFormLayout>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CameraInspector::CameraInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Camera"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

// ---------------------------------------------------------------------------
// Build UI
// ---------------------------------------------------------------------------

void CameraInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Projection
    m_projCombo = new QComboBox();
    m_projCombo->addItem(tr("Perspective"),  static_cast<int>(CameraProjection::Perspective));
    m_projCombo->addItem(tr("Orthographic"), static_cast<int>(CameraProjection::Orthographic));
    form->addRow(tr("Projection:"), m_projCombo);

    // FOV
    m_fovSpin = new QDoubleSpinBox();
    ConfigureMixedSpin(m_fovSpin, 1.0, 179.0, 1.0, 1);
    m_fovSpin->setSuffix("°");
    form->addRow(tr("FOV:"), m_fovSpin);

    m_viewModelFovSpin = new QDoubleSpinBox();
    ConfigureMixedSpin(m_viewModelFovSpin, 0.0, 179.0, 1.0, 1);
    m_viewModelFovSpin->setSuffix("°");
    m_viewModelFovSpin->setToolTip(tr("Field of view for first-person view model meshes. 0 uses FOV."));
    form->addRow(tr("View Model FOV:"), m_viewModelFovSpin);

    m_nearSpin = new QDoubleSpinBox();
    ConfigureMixedSpin(m_nearSpin, 0.001, 10000.0, 0.1, 3);
    form->addRow(tr("Near Plane:"), m_nearSpin);

    m_farSpin = new QDoubleSpinBox();
    ConfigureMixedSpin(m_farSpin, 0.1, 100000.0, 10.0, 1);
    form->addRow(tr("Far Plane:"), m_farSpin);

    // Active
    m_activeCheck = new QCheckBox(tr("Active"));
    form->addRow(QString(), m_activeCheck);

    // Show Editor Mesh
    m_showMeshCheck = new QCheckBox(tr("Show Editor Mesh"));
    form->addRow(QString(), m_showMeshCheck);

    mainLayout->addLayout(form);

    // Remove
    m_removeBtn = new QPushButton(tr("Remove Camera"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeBtn);
}

// ---------------------------------------------------------------------------
// Connect signals
// ---------------------------------------------------------------------------

void CameraInspector::connectSignals()
{
    connect(m_projCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyToComponent();
        EndEdit();
    });
    connect(m_fovSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_fovSpin,   &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_viewModelFovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_viewModelFovSpin, &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_nearSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_nearSpin,  &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_farSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_farSpin,   &QDoubleSpinBox::editingFinished, this, [this]() { EndEdit(); });
    connect(m_activeCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_showMeshCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove Camera");
        ForEachTarget<CameraComponent>([&](CameraComponent&, entt::entity entity) {
            auto& reg = m_ctx->GetActiveScene()->GetRegistry();
            if (reg.all_of<CameraComponent>(entity)) {
                reg.remove<CameraComponent>(entity);
            }
        });
        emit componentRemoved(m_entity, "Camera");
    });
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void CameraInspector::Refresh()
{
    EndEdit();
    MixedScalar fov, viewModelFov, nearPlane, farPlane;
    MixedBool active, showMesh;
    int firstProj = -1;
    bool mixedProj = false;
    VisitTargets<CameraComponent>([&](const CameraComponent& cam, entt::entity) {
        AccumulateScalar(fov, cam.fov);
        AccumulateScalar(viewModelFov, cam.viewModelFov);
        AccumulateScalar(nearPlane, cam.nearPlane);
        AccumulateScalar(farPlane, cam.farPlane);
        AccumulateBool(active, cam.isActive);
        AccumulateBool(showMesh, cam.showEditorMesh);
        const int proj = static_cast<int>(cam.projection);
        if (firstProj < 0) firstProj = proj;
        else if (firstProj != proj) mixedProj = true;
    });
    if (!fov.present) return;

    m_updating = true;
    blockAllSignals(true);
    SetComboFromMixed(m_projCombo, mixedProj, firstProj);
    SetSpinFromMixed(m_fovSpin, fov);
    SetSpinFromMixed(m_viewModelFovSpin, viewModelFov);
    SetSpinFromMixed(m_nearSpin, nearPlane);
    SetSpinFromMixed(m_farSpin, farPlane);
    SetCheckFromMixed(m_activeCheck, active);
    SetCheckFromMixed(m_showMeshCheck, showMesh);
    blockAllSignals(false);
    m_updating = false;
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

void CameraInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Edit Camera");
    int proj = 0;
    if (TryReadCombo(m_projCombo, proj)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.projection = static_cast<CameraProjection>(proj);
        });
    }
    double fov = 0.0, viewModelFov = 0.0, nearV = 0.0, farV = 0.0;
    if (TryReadSpin(m_viewModelFovSpin, viewModelFov)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.viewModelFov = static_cast<float>(viewModelFov);
        });
    }
    if (TryReadSpin(m_fovSpin, fov)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.fov = static_cast<float>(fov);
        });
    }
    if (TryReadSpin(m_nearSpin, nearV)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.nearPlane = static_cast<float>(nearV);
        });
    }
    if (TryReadSpin(m_farSpin, farV)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.farPlane = static_cast<float>(farV);
        });
    }
    bool active = false, showMesh = false;
    if (TryReadCheck(m_activeCheck, active)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.isActive = active;
        });
    }
    if (TryReadCheck(m_showMeshCheck, showMesh)) {
        ForEachTarget<CameraComponent>([&](CameraComponent& cam, entt::entity) {
            cam.showEditorMesh = showMesh;
        });
    }
}

// ---------------------------------------------------------------------------
// Signal blocking
// ---------------------------------------------------------------------------

void CameraInspector::blockAllSignals(bool block)
{
    m_projCombo->blockSignals(block);
    m_fovSpin->blockSignals(block);
    m_viewModelFovSpin->blockSignals(block);
    m_nearSpin->blockSignals(block);
    m_farSpin->blockSignals(block);
    m_activeCheck->blockSignals(block);
    m_showMeshCheck->blockSignals(block);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
