#if HEPHAESTUS_EDITOR_QT_UI

#include "PrefabHierarchyPanel.h"
#include <QHeaderView>

namespace Hephaestus {
namespace QtEditor {

PrefabHierarchyPanel::PrefabHierarchyPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QLabel(tr("Hierarchy"), this);
    header->setStyleSheet("QLabel { font-weight: bold; padding: 6px 8px; "
                          "background: #2a2a2a; border-bottom: 1px solid #3a3a3a; }");
    layout->addWidget(header);

    m_tree = new PrefabHierarchyTree(this);
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAnimated(true);
    m_tree->setIndentation(16);
    m_tree->setRootIsDecorated(true);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_tree->setStyleSheet(
        "QTreeWidget { background: #1e1e1e; border: none; outline: none; }"
        "QTreeWidget::item { padding: 3px 4px; border: none; }"
        "QTreeWidget::item:selected { background: #264f78; }"
        "QTreeWidget::item:hover { background: #2a2d2e; }"
    );

    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &PrefabHierarchyPanel::onSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &PrefabHierarchyPanel::onContextMenu);
    connect(m_tree, &PrefabHierarchyTree::itemsDropped,
            this, &PrefabHierarchyPanel::onItemsDropped);

    layout->addWidget(m_tree, 1);
}

void PrefabHierarchyPanel::Rebuild(const PrefabAsset& asset, const std::string& selectedNodeId) {
    m_tree->blockSignals(true);
    m_tree->clear();
    m_rootNodeId = asset.rootNodeId;

    if (!asset.rootNodeId.empty()) {
        populateItem(nullptr, asset, asset.rootNodeId, selectedNodeId);
    }

    m_tree->expandAll();
    m_tree->blockSignals(false);
}

void PrefabHierarchyPanel::SetSelectedNodeId(const std::string& nodeId) {
    m_tree->blockSignals(true);
    m_tree->clearSelection();

    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findItem;
    findItem = [&](QTreeWidgetItem* root) -> QTreeWidgetItem* {
        if (!root) return nullptr;
        if (root->data(0, Qt::UserRole).toString().toStdString() == nodeId)
            return root;
        for (int i = 0; i < root->childCount(); ++i) {
            auto* found = findItem(root->child(i));
            if (found) return found;
        }
        return nullptr;
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* found = findItem(m_tree->topLevelItem(i));
        if (found) {
            found->setSelected(true);
            m_tree->scrollToItem(found);
            break;
        }
    }

    m_tree->blockSignals(false);
}

std::string PrefabHierarchyPanel::GetSelectedNodeId() const {
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) return {};
    return items.first()->data(0, Qt::UserRole).toString().toStdString();
}

void PrefabHierarchyPanel::populateItem(QTreeWidgetItem* parent, const PrefabAsset& asset,
                                         const std::string& nodeId, const std::string& selectedNodeId) {
    const PrefabNode* node = nullptr;
    for (const auto& n : asset.nodes) {
        if (n.id == nodeId) { node = &n; break; }
    }
    if (!node) return;

    auto* item = parent
        ? new QTreeWidgetItem(parent)
        : new QTreeWidgetItem(m_tree);

    QString displayName = QString::fromStdString(node->name);
    if (nodeId == asset.rootNodeId) {
        displayName = QString::fromUtf8("\xE2\x97\x86 ") + displayName;  // diamond prefix for root
    }
    item->setText(0, displayName);
    item->setData(0, Qt::UserRole, QString::fromStdString(node->id));
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);

    if (nodeId == selectedNodeId) {
        item->setSelected(true);
    }

    for (const auto& child : asset.nodes) {
        if (child.id == nodeId) continue;
        bool isChild = (child.parentId == nodeId);
        if (!isChild && nodeId == asset.rootNodeId && child.parentId.empty()) {
            isChild = true;
        }
        if (isChild) {
            populateItem(item, asset, child.id, selectedNodeId);
        }
    }
}

void PrefabHierarchyPanel::onSelectionChanged() {
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) {
        emit selectionChanged({});
        return;
    }
    std::string nodeId = items.first()->data(0, Qt::UserRole).toString().toStdString();
    emit selectionChanged(nodeId);
}

void PrefabHierarchyPanel::onContextMenu(const QPoint& pos) {
    auto* item = m_tree->itemAt(pos);
    if (!item) return;

    std::string nodeId = item->data(0, Qt::UserRole).toString().toStdString();
    bool isRoot = (nodeId == m_rootNodeId);

    QMenu menu(this);

    menu.addAction(tr("Add Child Node"), this, [this, nodeId]() {
        emit addChildRequested(nodeId);
    });

    menu.addAction(tr("Duplicate"), this, [this, nodeId]() {
        emit duplicateNodeRequested(nodeId);
    })->setEnabled(!isRoot);

    menu.addSeparator();

    menu.addAction(tr("Rename"), this, [this, nodeId]() {
        emit renameNodeRequested(nodeId);
    });

    menu.addAction(tr("Reset Transform"), this, [this, nodeId]() {
        emit resetTransformRequested(nodeId);
    });

    menu.addAction(tr("Focus in Viewport"), this, [this, nodeId]() {
        emit focusInViewportRequested(nodeId);
    });

    menu.addSeparator();

    menu.addAction(tr("Delete"), this, [this, nodeId]() {
        emit deleteNodeRequested(nodeId);
    })->setEnabled(!isRoot);

    menu.exec(m_tree->mapToGlobal(pos));
}

void PrefabHierarchyPanel::onItemsDropped() {
    // After drag-drop, determine the new parent from the tree structure
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) return;

    auto* draggedItem = items.first();
    std::string nodeId = draggedItem->data(0, Qt::UserRole).toString().toStdString();

    std::string newParentId;
    if (auto* parentItem = draggedItem->parent()) {
        newParentId = parentItem->data(0, Qt::UserRole).toString().toStdString();
    } else {
        newParentId = m_rootNodeId;
    }

    emit reparentRequested(nodeId, newParentId);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
