#if HEPHAESTUS_EDITOR_QT_UI

#include "PrefabHierarchyPanel.h"
#include "HierarchyEye.h"
#include "../../UI/InspectorSelectionModel.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QColor>

namespace Hephaestus {
namespace QtEditor {

namespace {
constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kComponentTypeRole = Qt::UserRole + 2;
constexpr int kKindNode = 0;
constexpr int kKindComponent = 1;
}

PrefabHierarchyPanel::PrefabHierarchyPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* headerRow = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    auto* header = new QLabel(tr("Hierarchy"), headerRow);
    header->setStyleSheet("QLabel { font-weight: bold; padding: 6px 8px; "
                          "background: #2a2a2a; border-bottom: 1px solid #3a3a3a; }");
    headerLayout->addWidget(header, 1);

    m_addComponentBtn = new QPushButton(tr("+ Add"), headerRow);
    m_addComponentBtn->setToolTip(tr("Add a component to the selected node"));
    m_addComponentBtn->setCursor(Qt::PointingHandCursor);
    m_addComponentBtn->setStyleSheet(
        "QPushButton { padding: 6px 10px; font-weight: bold; background: #2a4a2a; "
        "border: none; border-bottom: 1px solid #3a3a3a; }"
        "QPushButton:hover { background: #3a5a3a; }"
        "QPushButton:pressed { background: #2a3a2a; }");
    connect(m_addComponentBtn, &QPushButton::clicked,
            this, &PrefabHierarchyPanel::onAddComponentClicked);
    headerLayout->addWidget(m_addComponentBtn);
    layout->addWidget(headerRow);

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
    HierarchyEye::Install(m_tree, [this](QTreeWidgetItem* item, bool isolate) {
        if (item->data(0, kKindRole).toInt() == kKindNode) {
            emit visibilityToggled(item->data(0, Qt::UserRole).toString().toStdString(), isolate);
        }
    });

    layout->addWidget(m_tree, 1);
}

void PrefabHierarchyPanel::Rebuild(const PrefabAsset& asset, const std::string& selectedNodeId) {
    m_tree->blockSignals(true);
    m_tree->clear();
    m_rootNodeId = asset.rootNodeId;

    if (!asset.rootNodeId.empty()) {
        populateItem(nullptr, asset, asset.rootNodeId, selectedNodeId, false, m_isolatedNode.empty());
    }

    m_tree->expandAll();
    m_tree->blockSignals(false);
}

void PrefabHierarchyPanel::SetVisibility(std::unordered_set<std::string> hiddenNodes, std::string isolatedNode) {
    m_hiddenNodes = std::move(hiddenNodes);
    m_isolatedNode = std::move(isolatedNode);
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
    auto* item = selectedItem();
    if (!item) return {};
    return item->data(0, Qt::UserRole).toString().toStdString();
}

std::string PrefabHierarchyPanel::GetSelectedComponentType() const {
    auto* item = selectedItem();
    if (!item || item->data(0, kKindRole).toInt() != kKindComponent) return {};
    return item->data(0, kComponentTypeRole).toString().toStdString();
}

QTreeWidgetItem* PrefabHierarchyPanel::selectedItem() const {
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) return nullptr;
    return items.first();
}

void PrefabHierarchyPanel::appendComponentItems(QTreeWidgetItem* nodeItem, const PrefabNode& node) {
    if (!nodeItem) return;
    for (const auto& entry : node.components) {
        if (entry.type.empty()) continue;
        auto* componentItem = new QTreeWidgetItem(nodeItem);
        const std::string display = InspectorDisplayName(entry.type);
        componentItem->setText(0, QString::fromUtf8("\xE2\x80\xA2 ") + QString::fromStdString(display));
        componentItem->setToolTip(0, tr("Component on %1")
            .arg(QString::fromStdString(node.name)));
        componentItem->setForeground(0, QColor("#9aa0a6"));
        componentItem->setData(0, Qt::UserRole, QString::fromStdString(node.id));
        componentItem->setData(0, kKindRole, kKindComponent);
        componentItem->setData(0, kComponentTypeRole, QString::fromStdString(entry.type));
        componentItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
}

void PrefabHierarchyPanel::populateItem(QTreeWidgetItem* parent, const PrefabAsset& asset,
                                         const std::string& nodeId, const std::string& selectedNodeId,
                                         bool parentHidden, bool underIsolated) {
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
    item->setData(0, kKindRole, kKindNode);
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    underIsolated = underIsolated || nodeId == m_isolatedNode;
    const bool hidden = m_hiddenNodes.contains(nodeId);
    HierarchyEye::State eye = HierarchyEye::State::Shown;
    if (hidden) {
        eye = HierarchyEye::State::Hidden;
    } else if (parentHidden || !underIsolated) {
        eye = HierarchyEye::State::HiddenByParent;
    }
    item->setData(0, HierarchyEye::kRole, static_cast<int>(eye));
    if (eye != HierarchyEye::State::Shown) {
        item->setForeground(0, QColor("#6f7378"));
    }

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
            populateItem(item, asset, child.id, selectedNodeId, parentHidden || hidden, underIsolated);
        }
    }

    appendComponentItems(item, *node);
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

void PrefabHierarchyPanel::onAddComponentClicked() {
    std::string nodeId = GetSelectedNodeId();
    if (nodeId.empty()) {
        nodeId = m_rootNodeId;
    }
    if (nodeId.empty()) return;
    emit addComponentRequested(nodeId);
}

void PrefabHierarchyPanel::onContextMenu(const QPoint& pos) {
    auto* item = m_tree->itemAt(pos);
    if (!item) return;

    std::string nodeId = item->data(0, Qt::UserRole).toString().toStdString();
    const bool isComponent = item->data(0, kKindRole).toInt() == kKindComponent;
    bool isRoot = (nodeId == m_rootNodeId);

    QMenu menu(this);

    if (isComponent) {
        const std::string type = item->data(0, kComponentTypeRole).toString().toStdString();
        menu.addAction(tr("Remove Component"), this, [this, nodeId, type]() {
            emit removeComponentRequested(nodeId, type);
        });
        menu.exec(m_tree->mapToGlobal(pos));
        return;
    }

    menu.addAction(tr("Add Component"), this, [this, nodeId]() {
        emit addComponentRequested(nodeId);
    });

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

    const bool hidden = m_hiddenNodes.contains(nodeId);
    menu.addAction(hidden ? tr("Show") : tr("Hide"), this, [this, nodeId]() {
        emit visibilityToggled(nodeId, false);
    });
    menu.addAction(m_isolatedNode == nodeId ? tr("Exit Isolation") : tr("Isolate"), this, [this, nodeId]() {
        emit visibilityToggled(nodeId, true);
    });
    menu.addAction(tr("Show All"), this, [this]() {
        emit showAllRequested();
    })->setEnabled(!m_hiddenNodes.empty() || !m_isolatedNode.empty());

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
    if (draggedItem->data(0, kKindRole).toInt() == kKindComponent) {
        return;
    }
    std::string nodeId = draggedItem->data(0, Qt::UserRole).toString().toStdString();

    std::string newParentId;
    if (auto* parentItem = draggedItem->parent()) {
        newParentId = parentItem->data(0, Qt::UserRole).toString().toStdString();
        if (parentItem->data(0, kKindRole).toInt() == kKindComponent) {
            if (auto* owner = parentItem->parent()) {
                newParentId = owner->data(0, Qt::UserRole).toString().toStdString();
            }
        }
    } else {
        newParentId = m_rootNodeId;
    }

    emit reparentRequested(nodeId, newParentId);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
