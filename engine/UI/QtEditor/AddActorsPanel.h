/**
 * @file AddActorsPanel.h
 * @brief "Add Actors" dockable panel for the Hephaestus Editor.
 *
 * Provides an Unreal-style panel where users can browse spawnable actor types
 * (Meshes, Lights, Cameras) organised by category and drag them into the
 * viewport to spawn new entities.
 */

#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QDockWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QSplitter>
#include <QLabel>
#include <QIcon>
#include <QString>
#include <QMimeData>
#include <QDrag>
#include <QJsonObject>
#include <QJsonDocument>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

class EditorMainWindow;

// ═══════════════════════════════════════════════════════════════
// Spawnable entry data model
// ═══════════════════════════════════════════════════════════════

/**
 * Describes one item that can be dragged from the Add Actors panel
 * into the viewport to spawn the corresponding entity/actor.
 */
struct SpawnableEntry {
    enum class Kind { Mesh, Light, Camera };

    Kind    kind        = Kind::Mesh;
    QString displayName;          ///< Human-readable name (e.g. "Cube", "Point Light")
    QString assetPath;            ///< Relative to runtime assets root (for Mesh/Camera)
    QString typeId;               ///< For Light or future types (e.g. "DirectionalLight")
    QIcon   icon;                 ///< Optional icon (placeholder for now)

    /// Serialise to JSON payload for QMimeData.
    QByteArray toMimePayload() const;
    /// Deserialise from JSON payload.
    static SpawnableEntry fromMimePayload(const QByteArray& data);
};

/// Custom MIME type used for drag-and-drop from the Add Actors panel.
inline constexpr const char* kSpawnableMimeType = "application/x-hephaestus-spawnable";

// ═══════════════════════════════════════════════════════════════
// Category model
// ═══════════════════════════════════════════════════════════════

/**
 * One category in the left-hand list (Meshes, Lights, etc.).
 */
struct SpawnableCategory {
    QString name;
    std::vector<SpawnableEntry> entries;
};

// ═══════════════════════════════════════════════════════════════
// Panel widget
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Dockable "Add Actors" panel widget.
 *
 * Layout:
 *   - Top:   Search bar (QLineEdit)
 *   - Left:  Category list (QListWidget)
 *   - Right: Item grid (QListWidget in icon mode) — draggable
 */
class AddActorsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AddActorsPanel(EditorMainWindow* mainWindow, QWidget* parent = nullptr);
    ~AddActorsPanel() override = default;

    /**
     * @brief Rebuild the spawnable entries.
     *
     * Scans the runtime assets/meshes directory for .glb files and
     * combines them with the hard-coded Lights category.
     */
    void Refresh();

signals:
    /// Emitted when the user wants to spawn a mesh from the panel.
    void spawnRequested(const SpawnableEntry& entry);

private:
    void setupUI();
    void buildCategories();
    void populateItemList();
    void applySearchFilter();

    EditorMainWindow* m_mainWindow = nullptr;

    // Data
    std::vector<SpawnableCategory> m_categories;
    int m_selectedCategory = 0;

    // UI
    QLineEdit*   m_searchBox     = nullptr;
    QSplitter*   m_splitter      = nullptr;
    QListWidget* m_categoryList  = nullptr;
    QListWidget* m_itemList      = nullptr;
    QLabel*      m_statusLabel   = nullptr;
};

/**
 * @brief Custom QListWidget that initiates drags with spawnable mime data.
 *
 * Each item carries a SpawnableEntry serialised into the drag mime payload.
 */
class DraggableItemList : public QListWidget
{
    Q_OBJECT
public:
    using QListWidget::QListWidget;

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
