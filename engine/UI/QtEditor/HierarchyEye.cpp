#if HEPHAESTUS_EDITOR_QT_UI

#include "HierarchyEye.h"
#include "EditorTheme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTreeWidget>

namespace Hephaestus {
namespace QtEditor {
namespace HierarchyEye {

namespace {

class ClickFilter final : public QObject
{
public:
    ClickFilter(QTreeWidget* tree, std::function<void(QTreeWidgetItem*, bool)> onClick)
        : QObject(tree), m_tree(tree), m_onClick(std::move(onClick)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick) {
            return QObject::eventFilter(watched, event);
        }
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const QPoint pos = mouse->position().toPoint();
        const QModelIndex index = m_tree->indexAt(pos);
        if (!index.isValid() || !index.data(kRole).isValid() ||
            !Rect(m_tree->visualRect(index.siblingAtColumn(0))).contains(pos)) {
            return false;
        }
        if (type == QEvent::MouseButtonPress) {
            if (QTreeWidgetItem* item = m_tree->itemFromIndex(index)) {
                m_onClick(item, (mouse->modifiers() & Qt::AltModifier) != 0);
            }
        }
        return true;
    }

private:
    QTreeWidget* m_tree;
    std::function<void(QTreeWidgetItem*, bool)> m_onClick;
};

} // namespace

QRect Rect(const QRect& row)
{
    return QRect(row.right() - kWidth + 4, row.center().y() - 8, 16, 16);
}

void Paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const QVariant data = index.data(kRole);
    if (!data.isValid()) {
        return;
    }
    const auto state = static_cast<State>(data.toInt());
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    if (state == State::Shown && !hovered) {
        return;
    }
    painter->save();
    if (state == State::HiddenByParent) {
        painter->setOpacity(0.45);
    }
    const QString name = state == State::Shown ? QStringLiteral("eye") : QStringLiteral("eye-off");
    const Theme::IconTone tone = state == State::Hidden ? Theme::IconTone::Secondary : Theme::IconTone::Muted;
    Theme::Icon(name, tone).paint(painter, Rect(option.rect));
    painter->restore();
}

void Delegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);
    Paint(painter, option, index);
}

void Install(QTreeWidget* tree, std::function<void(QTreeWidgetItem*, bool)> onClick, bool keepDelegate)
{
    if (!keepDelegate) {
        tree->setItemDelegate(new Delegate(tree));
    }
    tree->setMouseTracking(true);
    tree->viewport()->setAttribute(Qt::WA_Hover, true);
    tree->viewport()->installEventFilter(new ClickFilter(tree, std::move(onClick)));
}

} // namespace HierarchyEye
} // namespace QtEditor
} // namespace Hephaestus

#endif
