#pragma once

/**
 * @file MeshRendererInspector.h
 * @brief Inspector widget for MeshRendererComponent + MaterialComponent.
 *
 * Shows:
 *   - Visible / Show Hidden Outline / Hide-in-game / Backface Culling checkboxes
 *   - Mesh asset path (read-only, drag-drop target)
 *   - Material slots table: one row per submesh with assign/clear buttons
 *   - "Add Mesh Collider" button if no MeshColliderComponent
 *   - "Remove Mesh Component" button
 *
 * Material slot migration:
 *   Old scenes store a single material path. On display, if the mesh has N
 *   submeshes but appliedMaterialPaths has fewer entries, the array is
 *   padded with empty strings.  Slot 0 retains the legacy single material.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"
#include "AssetReferenceEditor.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QString>
#include <vector>

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

class MeshRendererInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit MeshRendererInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~MeshRendererInspector() override = default;

    void Refresh() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void rebuildMaterialSlots();
    std::vector<QString> collectProjectMaterialPaths() const;
    QString normalizeMaterialPathForSelection(const std::string& path) const;
    void setMaterialForSlot(int slot, const QString& materialPath);
    void clearMaterial(int slot);
    void blockAllSignals(bool block);

    // Widgets
    QCheckBox*      m_visibleCheck     = nullptr;
    QCheckBox*      m_showHiddenOutlineCheck = nullptr;
    QCheckBox*      m_hideInGameCheck  = nullptr;
    QCheckBox*      m_backfaceCullCheck = nullptr;
    AssetReferenceEditor* m_meshPathEdit = nullptr;
    QTableWidget*   m_materialTable    = nullptr;
    QPushButton*    m_addColliderBtn   = nullptr;
    QPushButton*    m_removeBtn        = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
