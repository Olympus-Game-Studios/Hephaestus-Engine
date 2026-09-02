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
#include "MaterialGraphQtEditorDialog.h"
#include "AssetPickerDialog.h"
#include "InspectorMime.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Assets/AssetTypes.h"
#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QMimeData>
#include <QStyle>
#include <QWidget>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unordered_set>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Ensure the appliedMaterialPaths vector has at least `count` entries.
 * Newly-added entries are empty strings ("No Material").
 */
static bool PathMissing(const std::string& portable)
{
    if (portable.empty()) return false;
    auto& assets = AssetManager::GetInstance();
    if (assets.GetAssetInfo(portable)) return false;
    if (portable.rfind("assets/", 0) == 0 && assets.GetAssetInfo(portable.substr(7))) {
        return false;
    }
    const auto resolved = std::filesystem::path(assets.ToAbsolutePath(
        portable.rfind("assets/", 0) == 0 ? portable.substr(7) : portable));
    return !std::filesystem::exists(resolved);
}

static QString ToPortableAsset(const QString& raw)
{
    auto& assets = AssetManager::GetInstance();
    std::string portable = AssetPathPolicy::ToPortableKey(assets.GetProjectRoot(), raw.toStdString());
    if (portable.empty()) portable = raw.toStdString();
    return QString::fromStdString(portable);
}

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

    // Visible + optional hidden-outline pick
    auto* visCol = new QWidget();
    auto* visLayout = new QVBoxLayout(visCol);
    visLayout->setContentsMargins(0, 0, 0, 0);
    visLayout->setSpacing(4);

    m_visibleCheck = new QCheckBox(tr("Visible"));
    visLayout->addWidget(m_visibleCheck);

    m_showHiddenOutlineCheck = new QCheckBox(tr("Show Hidden Outline"));
    m_showHiddenOutlineCheck->setToolTip(tr(
        "When Visible is off, draw this object's outline in the editor viewport and allow clicking it. "
        "If unchecked, the object stays fully hidden and cannot be picked."));
    m_showHiddenOutlineCheck->setVisible(false);
    visLayout->addWidget(m_showHiddenOutlineCheck);
    form->addRow(QString(), visCol);

    // Hide In Game
    m_hideInGameCheck = new QCheckBox(tr("Hide In Game"));
    form->addRow(QString(), m_hideInGameCheck);

    // Backface Culling
    m_backfaceCullCheck = new QCheckBox(tr("Backface Culling"));
    form->addRow(QString(), m_backfaceCullCheck);

    // Mesh asset path (read-only)
    m_meshPathEdit = new AssetReferenceEditor(QString(), AssetType::StaticMesh);
    m_meshPathEdit->setAllowEmpty(true);
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
    m_materialTable->viewport()->setAcceptDrops(true);
    m_materialTable->setMouseTracking(true);
    m_materialTable->viewport()->installEventFilter(this);
    m_materialTable->setStyleSheet("QTableWidget::item:hover { background: rgba(120, 170, 255, 45); }");
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
    connect(m_visibleCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_showHiddenOutlineCheck) {
            m_showHiddenOutlineCheck->setVisible(!checked);
        }
        applyToComponent();
        EndEdit();
    });
    connect(m_showHiddenOutlineCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_hideInGameCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_backfaceCullCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_meshPathEdit, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
        if (m_updating) return;
        BeginEdit("Assign mesh");
        ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) {
            mr.meshAssetPath = path.toStdString();
        });
        EndEdit();
    });

    // Add Mesh Collider
    connect(m_addColliderBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Add Mesh Collider");
        for (entt::entity entity : m_targets) {
            if (reg.valid(entity) && !reg.all_of<MeshColliderComponent>(entity)) {
                reg.emplace<MeshColliderComponent>(entity);
            }
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
        ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent&, entt::entity entity) {
            if (reg.all_of<MeshRendererComponent>(entity)) {
                reg.remove<MeshRendererComponent>(entity);
            }
            if (reg.all_of<MaterialComponent>(entity)) {
                reg.remove<MaterialComponent>(entity);
            }
        });
        emit componentRemoved(m_entity, "MeshRenderer");
    });
}

bool MeshRendererInspector::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_materialTable->viewport()) {
        return ComponentInspector::eventFilter(watched, event);
    }

    auto extractDrop = [](const QMimeData* mime, InspectorAssetDrop& out) {
        return ExtractInspectorAssetDrop(mime, out);
    };

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto* dragEvent = static_cast<QDragMoveEvent*>(event);
        InspectorAssetDrop drop;
        if (extractDrop(dragEvent->mimeData(), drop) &&
            AssetTypeCompatible(AssetType::Material, drop.type)) {
            dragEvent->acceptProposedAction();
            return true;
        }
    }

    if (event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        InspectorAssetDrop drop;
        if (!extractDrop(dropEvent->mimeData(), drop) ||
            !AssetTypeCompatible(AssetType::Material, drop.type)) {
            return false;
        }

        const int row = m_materialTable->rowAt(dropEvent->position().toPoint().y());
        if (row < 0) {
            return false;
        }

        setMaterialForSlot(row, drop.portablePath);
        rebuildMaterialSlots();
        dropEvent->acceptProposedAction();
        return true;
    }

    return ComponentInspector::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Refresh (component → UI)
// ---------------------------------------------------------------------------

void MeshRendererInspector::Refresh()
{
    EndEdit();
    MixedBool visible, outline, hideInGame, cull;
    MixedString meshPath;
    VisitTargets<MeshRendererComponent>([&](const MeshRendererComponent& mr, entt::entity) {
        AccumulateBool(visible, mr.visible);
        AccumulateBool(outline, mr.showHiddenOutline);
        AccumulateBool(hideInGame, mr.hideInGame);
        AccumulateBool(cull, mr.backfaceCulling);
        AccumulateString(meshPath, mr.meshAssetPath);
    });
    if (!visible.present) return;

    m_updating = true;
    blockAllSignals(true);
    SetCheckFromMixed(m_visibleCheck, visible);
    SetCheckFromMixed(m_showHiddenOutlineCheck, outline);
    m_showHiddenOutlineCheck->setVisible(!visible.mixed && !visible.value);
    SetCheckFromMixed(m_hideInGameCheck, hideInGame);
    SetCheckFromMixed(m_backfaceCullCheck, cull);
    if (meshPath.mixed) {
        m_meshPathEdit->setPath(QString(), true, false);
    } else {
        m_meshPathEdit->setPath(QString::fromStdString(meshPath.value), false,
                                PathMissing(meshPath.value));
    }
    bool allHaveCollider = true;
    VisitTargets<MeshRendererComponent>([&](const MeshRendererComponent&, entt::entity entity) {
        if (!m_ctx->GetActiveScene()->GetRegistry().all_of<MeshColliderComponent>(entity)) {
            allHaveCollider = false;
        }
    });
    m_addColliderBtn->setVisible(!allHaveCollider);
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

    const auto& mr = reg.get<MeshRendererComponent>(m_entity);
    std::vector<QString> materialOptions = collectProjectMaterialPaths();

    int slotCount = std::max(mr.submeshCount, 1);
    m_materialTable->setRowCount(slotCount);

    for (int i = 0; i < slotCount; ++i) {
        QString selectedPath;
        if (i < static_cast<int>(mr.appliedMaterialPaths.size()) &&
            !mr.appliedMaterialPaths[i].empty()) {
            selectedPath = normalizeMaterialPathForSelection(mr.appliedMaterialPaths[i]);
        }

        // Column 0 — Slot index/name
        auto* slotItem = new QTableWidgetItem(selectedPath.isEmpty()
            ? QString("Slot %1\n%2").arg(i).arg(tr("Fallback material"))
            : QString("Slot %1").arg(i));
        slotItem->setFlags(Qt::ItemIsEnabled);
        slotItem->setToolTip(tr("Material slot %1. Drop a material asset here to assign it.").arg(i));
        m_materialTable->setItem(i, 0, slotItem);

        // Column 1 — Project material dropdown
        auto* materialCombo = new QComboBox();
        materialCombo->addItem(tr("(No Material)"), QString());
        for (const auto& materialPath : materialOptions) {
            materialCombo->addItem(materialPath, materialPath);
        }

        int selectedIndex = materialCombo->findData(selectedPath);
        materialCombo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
        materialCombo->setToolTip(selectedPath.isEmpty() ? tr("Using renderer fallback material.") : selectedPath);
        connect(materialCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i, materialCombo](int) {
                    setMaterialForSlot(i, materialCombo->currentData().toString());
                });
        m_materialTable->setCellWidget(i, 1, materialCombo);

        // Column 2 — Clear button
        auto* btnWidget = new QWidget();
        auto* btnLayout = new QHBoxLayout(btnWidget);
        btnLayout->setContentsMargins(2, 0, 2, 0);
        btnLayout->setSpacing(2);

        auto* openBtn = new QPushButton(tr("Open"));
        openBtn->setFixedHeight(22);
        openBtn->setEnabled(!selectedPath.isEmpty());
        openBtn->setToolTip(tr("Open the assigned material in the material editor."));
        connect(openBtn, &QPushButton::clicked, this, [this, selectedPath]() {
            if (selectedPath.isEmpty()) return;
            const std::string absolute = AssetManager::GetInstance().ToAbsolutePath(selectedPath.toStdString());
            OpenMaterialGraphEditorDialog(window(), QString::fromStdString(absolute));
        });

        auto* assignBtn = new QPushButton(tr("Assign"));
        assignBtn->setFixedHeight(22);
        assignBtn->setToolTip(tr("Choose a material file for this slot."));
        connect(assignBtn, &QPushButton::clicked, this, [this, i]() {
            auto chosen = AssetPickerDialog::Choose(AssetType::Material, this, QString());
            if (!chosen.has_value()) return;
            setMaterialForSlot(i, chosen.value());
            rebuildMaterialSlots();
        });

        auto* clearBtn = new QPushButton(tr("Clear"));
        clearBtn->setFixedHeight(22);
        clearBtn->setToolTip(tr("Clear this material slot and use the fallback material."));
        connect(clearBtn, &QPushButton::clicked, this, [this, i]() { clearMaterial(i); });

        btnLayout->addWidget(openBtn);
        btnLayout->addWidget(assignBtn);
        btnLayout->addWidget(clearBtn);
        m_materialTable->setCellWidget(i, 2, btnWidget);
    }
}

std::vector<QString> MeshRendererInspector::collectProjectMaterialPaths() const
{
    auto& assetManager = AssetManager::GetInstance();
    if (assetManager.HasValidAssetRoot()) {
        assetManager.Refresh();
    }

    auto materials = assetManager.GetAssetsByType(AssetType::Material);

    std::vector<QString> paths;
    std::unordered_set<std::string> dedupe;

    auto addUniquePath = [&paths, &dedupe](const QString& rawPath) {
        if (rawPath.isEmpty()) return;

        QString normalized = rawPath;
        normalized.replace('\\', '/');

        const std::string key = normalized.toLower().toStdString();
        if (dedupe.insert(key).second) {
            paths.push_back(normalized);
        }
    };

    for (const auto& material : materials) {
        if (!material.relativePath.empty()) {
            addUniquePath(QString::fromStdString(material.relativePath));
        }
    }

    namespace fs = std::filesystem;
    const fs::path projectRoot = fs::path(assetManager.GetProjectRoot());
    const fs::path assetRoot = fs::path(assetManager.GetAssetRoot());

    if (!projectRoot.empty() && fs::exists(projectRoot)) {
        std::error_code ec;
        fs::recursive_directory_iterator it(projectRoot, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;

        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec)) {
                ec.clear();
                continue;
            }

            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            const bool isMaterialFile =
                ext == ".hematerial" ||
                ext == ".hpmat" ||
                ext == ".material" ||
                ext == ".mat";

            if (!isMaterialFile) {
                continue;
            }

            const fs::path filePath = it->path().lexically_normal();
            const QString filePathString = QString::fromStdString(filePath.generic_string());

            if (!assetRoot.empty()) {
                const QString assetRootString =
                    QString::fromStdString(assetRoot.lexically_normal().generic_string());

                if (filePathString.compare(assetRootString, Qt::CaseInsensitive) == 0) {
                    continue;
                }

                if (filePathString.startsWith(assetRootString + "/", Qt::CaseInsensitive)) {
                    addUniquePath(filePathString.mid(assetRootString.size() + 1));
                    continue;
                }
            }

            addUniquePath(filePathString);
        }
    }

    std::sort(paths.begin(), paths.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

QString MeshRendererInspector::normalizeMaterialPathForSelection(const std::string& path) const
{
    QString normalized = QString::fromStdString(path);
    normalized.replace('\\', '/');

    const QString assetRoot = QString::fromStdString(AssetManager::GetInstance().GetAssetRoot()).replace('\\', '/');
    if (!assetRoot.isEmpty() && normalized.startsWith(assetRoot + '/')) {
        normalized = normalized.mid(assetRoot.size() + 1);
    }

    if (normalized.startsWith("assets/", Qt::CaseInsensitive)) {
        normalized = normalized.mid(7);
    }

    return normalized;
}

void MeshRendererInspector::setMaterialForSlot(int slot, const QString& materialPath)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    const QString portable = ToPortableAsset(materialPath);
    const std::string newPath = portable.toStdString();
    BeginEdit("Assign material slot");
    ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) {
        EnsureMaterialSlots(mr);
        if (slot >= static_cast<int>(mr.appliedMaterialPaths.size())) return;
        mr.appliedMaterialPaths[slot] = newPath;
        if (newPath.empty() && slot < static_cast<int>(mr.materialIndices.size())) {
            mr.materialIndices[slot] = -1;
        }
    });
    EndEdit();
}

void MeshRendererInspector::clearMaterial(int slot)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Clear material slot");
    ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) {
        EnsureMaterialSlots(mr);
        if (slot < static_cast<int>(mr.appliedMaterialPaths.size())) {
            mr.appliedMaterialPaths[slot].clear();
        }
        if (slot < static_cast<int>(mr.materialIndices.size())) {
            mr.materialIndices[slot] = -1;
        }
    });
    EndEdit();
    rebuildMaterialSlots();
}

void MeshRendererInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Edit MeshRenderer");
    bool value = false;
    if (TryReadCheck(m_visibleCheck, value)) {
        ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) { mr.visible = value; });
    }
    if (TryReadCheck(m_showHiddenOutlineCheck, value)) {
        ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) { mr.showHiddenOutline = value; });
    }
    if (TryReadCheck(m_hideInGameCheck, value)) {
        ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) { mr.hideInGame = value; });
    }
    if (TryReadCheck(m_backfaceCullCheck, value)) {
        ForEachTarget<MeshRendererComponent>([&](MeshRendererComponent& mr, entt::entity) { mr.backfaceCulling = value; });
    }
}

// ---------------------------------------------------------------------------
// Signal blocking
// ---------------------------------------------------------------------------

void MeshRendererInspector::blockAllSignals(bool block)
{
    m_visibleCheck->blockSignals(block);
    m_showHiddenOutlineCheck->blockSignals(block);
    m_hideInGameCheck->blockSignals(block);
    m_backfaceCullCheck->blockSignals(block);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
