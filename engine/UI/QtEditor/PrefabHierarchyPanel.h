#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QLabel>
#include <QDropEvent>
#include <QMimeData>
#include <functional>
#include <string>
#include <unordered_set>

#include "../../Scene/PrefabAsset.h"

namespace Hephaestus {
namespace QtEditor {

class PrefabHierarchyTree : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;
signals:
    void itemsDropped();
protected:
    void dropEvent(QDropEvent* event) override {
        QTreeWidget::dropEvent(event);
        if (event->isAccepted()) {
            emit itemsDropped();
        }
    }
};

class PrefabHierarchyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PrefabHierarchyPanel(QWidget* parent = nullptr);

    void Rebuild(const PrefabAsset& asset, const std::string& selectedNodeId);
    /// Editor-only visibility the rows' eyes show from the next Rebuild (node ids; isolated "" when not isolating).
    void SetVisibility(std::unordered_set<std::string> hiddenNodes, std::string isolatedNode);
    void SetSelectedNodeId(const std::string& nodeId);
    std::string GetSelectedNodeId() const;
    std::string GetSelectedComponentType() const;

signals:
    void selectionChanged(const std::string& nodeId);
    void addChildRequested(const std::string& parentNodeId);
    void addComponentRequested(const std::string& nodeId);
    void removeComponentRequested(const std::string& nodeId, const std::string& componentType);
    void deleteNodeRequested(const std::string& nodeId);
    void duplicateNodeRequested(const std::string& nodeId);
    void renameNodeRequested(const std::string& nodeId);
    void resetTransformRequested(const std::string& nodeId);
    void focusInViewportRequested(const std::string& nodeId);
    void reparentRequested(const std::string& nodeId, const std::string& newParentId);
    void visibilityToggled(const std::string& nodeId, bool isolate);
    void showAllRequested();

private slots:
    void onSelectionChanged();
    void onContextMenu(const QPoint& pos);
    void onItemsDropped();
    void onAddComponentClicked();

private:
    void populateItem(QTreeWidgetItem* parent, const PrefabAsset& asset,
                      const std::string& nodeId, const std::string& selectedNodeId,
                      bool parentHidden, bool underIsolated);
    void appendComponentItems(QTreeWidgetItem* nodeItem, const PrefabNode& node);
    QTreeWidgetItem* selectedItem() const;

    PrefabHierarchyTree* m_tree = nullptr;
    QPushButton* m_addComponentBtn = nullptr;
    std::string m_rootNodeId;
    std::unordered_set<std::string> m_hiddenNodes;
    std::string m_isolatedNode;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
