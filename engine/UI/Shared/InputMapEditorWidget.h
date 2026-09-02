#pragma once

#include "../../Input/InputMap.h"

#include <QWidget>

#include <filesystem>
#include <functional>

class QLabel;
class QPushButton;
class QString;
class QTreeWidget;
class QTreeWidgetItem;

namespace Hephaestus::UI {

class InputMapEditorWidget : public QWidget {
public:
    explicit InputMapEditorWidget(QWidget* parent = nullptr);

    bool Load(const std::filesystem::path& projectRoot,
              const QString& profilePath,
              QString* error = nullptr);
    bool Save(const std::filesystem::path& projectRoot,
              const QString& profilePath,
              QString* error = nullptr);
    bool IsDirty() const { return m_dirty; }
    void SetChangedCallback(std::function<void()> callback);

private:
    void RebuildTree();
    void SyncFromTree();
    void MarkDirty();
    QTreeWidgetItem* SelectedDefinition() const;

    InputMap m_map;
    bool m_dirty = false;
    bool m_rebuilding = false;
    std::function<void()> m_changed;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_addAction = nullptr;
    QPushButton* m_addAxis = nullptr;
    QPushButton* m_addBinding = nullptr;
    QPushButton* m_remove = nullptr;
};

} // namespace Hephaestus::UI
