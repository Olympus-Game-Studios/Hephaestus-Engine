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

#include "../../Assets/AssetManager.h"
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

bool MeshRendererInspector::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_materialTable->viewport()) {
        return ComponentInspector::eventFilter(watched, event);
    }

    auto isMaterialPath = [](QString path) {
        path.replace('\\', '/');
        const QString ext = QFileInfo(path).suffix().toLower();
        return ext == "hematerial" || ext == "hpmat" || ext == "material" || ext == "mat";
    };

    auto extractPath = [](const QMimeData* mime) {
        if (!mime) return QString();
        if (mime->hasUrls() && !mime->urls().isEmpty()) {
            return mime->urls().front().toLocalFile();
        }
        if (mime->hasText()) {
            return mime->text().trimmed();
        }
        return QString();
    };

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto* dragEvent = static_cast<QDragMoveEvent*>(event);
        const QString path = extractPath(dragEvent->mimeData());
        if (isMaterialPath(path)) {
            dragEvent->acceptProposedAction();
            return true;
        }
    }

    if (event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        QString path = extractPath(dropEvent->mimeData());
        if (!isMaterialPath(path)) {
            return false;
        }

        const int row = m_materialTable->rowAt(dropEvent->position().toPoint().y());
        if (row < 0) {
            return false;
        }

        path.replace('\\', '/');
        const QString assetRoot = QString::fromStdString(AssetManager::GetInstance().GetAssetRoot()).replace('\\', '/');
        if (!assetRoot.isEmpty() && path.startsWith(assetRoot + "/", Qt::CaseInsensitive)) {
            path = path.mid(assetRoot.size() + 1);
        }
        setMaterialForSlot(row, path);
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
            const QString file = QFileDialog::getOpenFileName(this, tr("Assign Material"), QString::fromStdString(AssetManager::GetInstance().GetAssetRoot()),
                tr("Materials (*.hematerial *.hpmat *.material *.mat);;All Files (*.*)"));
            if (file.isEmpty()) return;
            QString normalized = file;
            normalized.replace('\\', '/');
            const QString assetRoot = QString::fromStdString(AssetManager::GetInstance().GetAssetRoot()).replace('\\', '/');
            if (!assetRoot.isEmpty() && normalized.startsWith(assetRoot + "/", Qt::CaseInsensitive)) {
                normalized = normalized.mid(assetRoot.size() + 1);
            }
            setMaterialForSlot(i, normalized);
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
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<MeshRendererComponent>(m_entity)) return;

    auto& mr = reg.get<MeshRendererComponent>(m_entity);
    EnsureMaterialSlots(mr);

    if (slot >= static_cast<int>(mr.appliedMaterialPaths.size())) return;

    const std::string newPath = materialPath.toStdString();
    if (mr.appliedMaterialPaths[slot] == newPath) {
        return;
    }

    EditorUndoManager::PushSnapshot(m_ctx, "Assign material slot " + std::to_string(slot));
    mr.appliedMaterialPaths[slot] = newPath;

    if (newPath.empty() && slot < static_cast<int>(mr.materialIndices.size())) {
        mr.materialIndices[slot] = -1;
    }

    m_ctx->MarkDirty();
}

// ---------------------------------------------------------------------------
// Assign / Clear material on a slot
// ---------------------------------------------------------------------------

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
