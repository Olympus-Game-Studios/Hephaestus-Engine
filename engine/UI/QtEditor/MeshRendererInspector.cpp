/**
 * @file MeshRendererInspector.cpp
 * @brief Implementation of the MeshRendererComponent + material-slots inspector.
 *
 * Material slot migration rule:
 *   If MeshRendererComponent::submeshCount > appliedMaterialPaths.size(),
 *   we pad the vector with empty strings at display time.  Slot 0 may
 *   already contain the legacy single-material path from older scenes.
 *   When saving, the full appliedMaterialPaths array is written, so the
 *   migration is transparent and backwards-compatible.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "MeshRendererInspector.h"
#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QStyle>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Ensure the appliedMaterialPaths vector has at least `count` entries.
 * Newly-added entries are empty strings ("No Material").
 */
static void EnsureMaterialSlots(MeshRendererComponent& mr)
{
    int need = std::max(mr.submeshCount, 1);
    while (static_cast<int>(mr.appliedMaterialPaths.size()) < need) {
        mr.appliedMaterialPaths.push_back("");
    }
    while (static_cast<int>(mr.materialIndices.size()) < need) {
        mr.materialIndices.push_back(-1);
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MeshRendererInspector::MeshRendererInspector(EditorContext* ctx,
                                             entt::entity entity,
                                             QWidget* parent)
    : ComponentInspector(tr("Mesh Renderer"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

// ---------------------------------------------------------------------------
// Build UI
// ---------------------------------------------------------------------------

void MeshRendererInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Visible
    m_visibleCheck = new QCheckBox(tr("Visible"));
    form->addRow(QString(), m_visibleCheck);

    // Hide In Game
    m_hideInGameCheck = new QCheckBox(tr("Hide In Game"));
    form->addRow(QString(), m_hideInGameCheck);

    // Backface Culling
    m_backfaceCullCheck = new QCheckBox(tr("Backface Culling"));
    form->addRow(QString(), m_backfaceCullCheck);

    // Mesh asset path (read-only)
    m_meshPathEdit = new QLineEdit();
    m_meshPathEdit->setReadOnly(true);
    m_meshPathEdit->setPlaceholderText(tr("(None)"));
    m_meshPathEdit->setToolTip(tr("Mesh asset path (drag a mesh from Asset Browser to assign)"));
    form->addRow(tr("Mesh:"), m_meshPathEdit);

    mainLayout->addLayout(form);

    // ── Material Slots Table ────────────────────────────────────────────
    auto* matLabel = new QLabel(tr("Materials"));
    matLabel->setStyleSheet("font-weight: bold; margin-top: 6px;");
    mainLayout->addWidget(matLabel);

    m_materialTable = new QTableWidget(0, 3); // columns: Slot, Material, Actions
    m_materialTable->setHorizontalHeaderLabels({ tr("Slot"), tr("Material"), tr("") });
    m_materialTable->horizontalHeader()->setStretchLastSection(false);
    m_materialTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_materialTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_materialTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_materialTable->verticalHeader()->setVisible(false);
    m_materialTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_materialTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_materialTable->setMaximumHeight(180);
    m_materialTable->setAcceptDrops(true);
    mainLayout->addWidget(m_materialTable);

    // ── Buttons ─────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    m_addColliderBtn = new QPushButton(tr("Add Mesh Collider"));
    m_removeBtn = new QPushButton(tr("Remove Mesh Component"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    btnRow->addWidget(m_addColliderBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_removeBtn);
    mainLayout->addLayout(btnRow);
}

// ---------------------------------------------------------------------------
// Connect signals
// ---------------------------------------------------------------------------

void MeshRendererInspector::connectSignals()
{
    connect(m_visibleCheck,      &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_hideInGameCheck,   &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_backfaceCullCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });

    // Add Mesh Collider
    connect(m_addColliderBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Add Mesh Collider");

        if (!reg.all_of<MeshColliderComponent>(m_entity)) {
            reg.emplace<MeshColliderComponent>(m_entity);
        }
        m_ctx->MarkDirty();
        // The parent Details panel will re-create inspectors on next refresh
        emit componentRemoved(m_entity, "MeshColliderAdded"); // signal to rebuild
    });

    // Remove Mesh Component
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove MeshRenderer");

        if (reg.all_of<MeshRendererComponent>(m_entity)) {
            reg.remove<MeshRendererComponent>(m_entity);
        }
        if (reg.all_of<MaterialComponent>(m_entity)) {
            reg.remove<MaterialComponent>(m_entity);
        }
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "MeshRenderer");
    });
}

// ---------------------------------------------------------------------------
// Refresh (component → UI)
// ---------------------------------------------------------------------------

void MeshRendererInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    auto& mr = reg.get<MeshRendererComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    m_visibleCheck->setChecked(mr.visible);
    m_hideInGameCheck->setChecked(mr.hideInGame);
    m_backfaceCullCheck->setChecked(mr.backfaceCulling);

    // Mesh path
    if (!mr.meshAssetPath.empty()) {
        m_meshPathEdit->setText(QString::fromStdString(mr.meshAssetPath));
    } else if (mr.meshHandle != MeshRendererComponent::InvalidMesh) {
        m_meshPathEdit->setText(tr("Built-in mesh"));
    } else {
        m_meshPathEdit->clear();
    }

    // Show/hide "Add Mesh Collider" button
    m_addColliderBtn->setVisible(!reg.all_of<MeshColliderComponent>(m_entity));

    blockAllSignals(false);
    m_updating = false;

    rebuildMaterialSlots();
}

// ---------------------------------------------------------------------------
// Material slots table
// ---------------------------------------------------------------------------

void MeshRendererInspector::rebuildMaterialSlots()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    auto& mr = reg.get<MeshRendererComponent>(m_entity);
    EnsureMaterialSlots(mr);

    int slotCount = std::max(mr.submeshCount, 1);
    m_materialTable->setRowCount(slotCount);

    for (int i = 0; i < slotCount; ++i) {
        // Column 0 — Slot index
        auto* slotItem = new QTableWidgetItem(QString("Slot %1").arg(i));
        slotItem->setFlags(Qt::ItemIsEnabled);
        m_materialTable->setItem(i, 0, slotItem);

        // Column 1 — Material path or "(No Material)"
        QString matPath;
        if (i < static_cast<int>(mr.appliedMaterialPaths.size()) &&
            !mr.appliedMaterialPaths[i].empty()) {
            matPath = QString::fromStdString(mr.appliedMaterialPaths[i]);
        } else {
            matPath = tr("(No Material)");
        }
        auto* matItem = new QTableWidgetItem(matPath);
        matItem->setFlags(Qt::ItemIsEnabled);
        if (matPath == tr("(No Material)")) {
            matItem->setForeground(Qt::gray);
        }
        m_materialTable->setItem(i, 1, matItem);

        // Column 2 — Assign + Clear buttons
        auto* btnWidget = new QWidget();
        auto* btnLayout = new QHBoxLayout(btnWidget);
        btnLayout->setContentsMargins(2, 0, 2, 0);
        btnLayout->setSpacing(2);

        auto* assignBtn = new QPushButton(tr("Assign"));
        assignBtn->setFixedHeight(22);
        connect(assignBtn, &QPushButton::clicked, this, [this, i]() { assignMaterial(i); });

        auto* clearBtn = new QPushButton(tr("Clear"));
        clearBtn->setFixedHeight(22);
        connect(clearBtn, &QPushButton::clicked, this, [this, i]() { clearMaterial(i); });

        btnLayout->addWidget(assignBtn);
        btnLayout->addWidget(clearBtn);
        m_materialTable->setCellWidget(i, 2, btnWidget);
    }
}

// ---------------------------------------------------------------------------
// Assign / Clear material on a slot
// ---------------------------------------------------------------------------

void MeshRendererInspector::assignMaterial(int slot)
{
    QString filepath = QFileDialog::getOpenFileName(
        this, tr("Assign Material to Slot %1").arg(slot),
        QString("assets"),
        tr("Material Files (*.hpmat);;All Files (*)"));

    if (filepath.isEmpty()) return;

    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Assign material slot " + std::to_string(slot));

    auto& mr = reg.get<MeshRendererComponent>(m_entity);
    EnsureMaterialSlots(mr);

    if (slot < static_cast<int>(mr.appliedMaterialPaths.size())) {
        mr.appliedMaterialPaths[slot] = filepath.toStdString();
    }

    m_ctx->MarkDirty();
    rebuildMaterialSlots();
}

void MeshRendererInspector::clearMaterial(int slot)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Clear material slot " + std::to_string(slot));

    auto& mr = reg.get<MeshRendererComponent>(m_entity);
    EnsureMaterialSlots(mr);

    if (slot < static_cast<int>(mr.appliedMaterialPaths.size())) {
        mr.appliedMaterialPaths[slot] = "";
        if (slot < static_cast<int>(mr.materialIndices.size())) {
            mr.materialIndices[slot] = -1;
        }
    }

    m_ctx->MarkDirty();
    rebuildMaterialSlots();
}

// ---------------------------------------------------------------------------
// Apply (UI → component)
// ---------------------------------------------------------------------------

void MeshRendererInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Edit MeshRenderer");

    auto& mr = reg.get<MeshRendererComponent>(m_entity);
    mr.visible        = m_visibleCheck->isChecked();
    mr.hideInGame     = m_hideInGameCheck->isChecked();
    mr.backfaceCulling = m_backfaceCullCheck->isChecked();

    m_ctx->MarkDirty();
}

// ---------------------------------------------------------------------------
// Signal blocking
// ---------------------------------------------------------------------------

void MeshRendererInspector::blockAllSignals(bool block)
{
    m_visibleCheck->blockSignals(block);
    m_hideInGameCheck->blockSignals(block);
    m_backfaceCullCheck->blockSignals(block);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
