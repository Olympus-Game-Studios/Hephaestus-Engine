#pragma once

#include "../../Physics/PhysicsLayerConfig.h"

#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace Hephaestus::UI {

class PhysicsLayerEditorWidget : public QWidget {
public:
    explicit PhysicsLayerEditorWidget(QWidget* parent = nullptr);

    bool Load(const std::filesystem::path& projectRoot, QString* error = nullptr);
    bool Save(QString* error = nullptr);
    bool IsDirty() const { return m_dirty; }
    void SetChangedCallback(std::function<void()> callback);

private:
    void Rebuild();
    void RebuildMatrix();
    void UpdateStatus();
    void MarkDirty();
    void OnNameEdited(QTableWidgetItem* item);
    void OnAddLayer();
    void OnDeleteLayer();

    Physics::PhysicsLayerConfig m_config;
    std::filesystem::path m_projectRoot;
    std::vector<std::uint16_t> m_matrixIds;
    bool m_dirty = false;
    bool m_rebuilding = false;
    std::function<void()> m_changed;

    QTableWidget* m_layers = nullptr;
    QTableWidget* m_matrix = nullptr;
    QLineEdit* m_newName = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_noteLabel = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace Hephaestus::UI
