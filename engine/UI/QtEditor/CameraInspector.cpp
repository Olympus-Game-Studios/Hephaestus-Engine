/**
 * @file CameraInspector.cpp
 * @brief Implementation of the CameraComponent inspector.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "CameraInspector.h"
#include "EditorUndoCommand.h"

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
    m_fovSpin->setRange(1.0, 179.0);
    m_fovSpin->setSingleStep(1.0);
    m_fovSpin->setDecimals(1);
    m_fovSpin->setSuffix("°");
    form->addRow(tr("FOV:"), m_fovSpin);

    // Near plane
    m_nearSpin = new QDoubleSpinBox();
    m_nearSpin->setRange(0.001, 10000.0);
    m_nearSpin->setSingleStep(0.1);
    m_nearSpin->setDecimals(3);
    form->addRow(tr("Near Plane:"), m_nearSpin);

    // Far plane
    m_farSpin = new QDoubleSpinBox();
    m_farSpin->setRange(0.1, 100000.0);
    m_farSpin->setSingleStep(10.0);
    m_farSpin->setDecimals(1);
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
    connect(m_projCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyToComponent(); });
    connect(m_fovSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_nearSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_farSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyToComponent(); });
    connect(m_activeCheck,   &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_showMeshCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove Camera");
        if (reg.all_of<CameraComponent>(m_entity)) reg.remove<CameraComponent>(m_entity);
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "Camera");
    });
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void CameraInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<CameraComponent>(m_entity)) return;

    const auto& cam = reg.get<CameraComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    int projIdx = (cam.projection == CameraProjection::Perspective) ? 0 : 1;
    m_projCombo->setCurrentIndex(projIdx);
    m_fovSpin->setValue(static_cast<double>(cam.fov));
    m_nearSpin->setValue(static_cast<double>(cam.nearPlane));
    m_farSpin->setValue(static_cast<double>(cam.farPlane));
    m_activeCheck->setChecked(cam.isActive);
    m_showMeshCheck->setChecked(cam.showEditorMesh);

    blockAllSignals(false);
    m_updating = false;
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

void CameraInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<CameraComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Edit Camera");

    auto& cam = reg.get<CameraComponent>(m_entity);
    cam.projection    = static_cast<CameraProjection>(m_projCombo->currentData().toInt());
    cam.fov           = static_cast<float>(m_fovSpin->value());
    cam.nearPlane     = static_cast<float>(m_nearSpin->value());
    cam.farPlane      = static_cast<float>(m_farSpin->value());
    cam.isActive      = m_activeCheck->isChecked();
    cam.showEditorMesh = m_showMeshCheck->isChecked();

    m_ctx->MarkDirty();
}

// ---------------------------------------------------------------------------
// Signal blocking
// ---------------------------------------------------------------------------

void CameraInspector::blockAllSignals(bool block)
{
    m_projCombo->blockSignals(block);
    m_fovSpin->blockSignals(block);
    m_nearSpin->blockSignals(block);
    m_farSpin->blockSignals(block);
    m_activeCheck->blockSignals(block);
    m_showMeshCheck->blockSignals(block);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
