#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QModelIndex>
#include <QRect>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <functional>

class QPainter;
class QTreeWidget;
class QTreeWidgetItem;

namespace Hephaestus {
namespace QtEditor {
namespace HierarchyEye {

/// What a hierarchy row's eye shows. Rows without kRole data get no eye.
enum class State { Shown = 0, Hidden = 1, HiddenByParent = 2 };
constexpr int kRole = Qt::UserRole + 40;

/// Room the eye takes at the right of a row, for other trailing markers to stay clear of.
constexpr int kWidth = 24;

/// The eye's rectangle in a row's visual rectangle.
QRect Rect(const QRect& row);

/// Draws the eye: always for hidden rows (eye-off, dimmed when hidden by a parent or by isolation), on hover otherwise.
void Paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index);

/// A delegate that draws the default row and then the eye, for trees with no delegate of their own.
class Delegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// Clicking a row's eye calls `onClick(item, isolate)` instead of selecting the row; Alt+click asks to isolate.
/// Installs the Delegate too unless `keepDelegate` (when the tree's own delegate calls Paint).
void Install(QTreeWidget* tree, std::function<void(QTreeWidgetItem*, bool isolate)> onClick, bool keepDelegate = false);

} // namespace HierarchyEye
} // namespace QtEditor
} // namespace Hephaestus

#endif
