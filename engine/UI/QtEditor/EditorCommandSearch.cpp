#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorCommandSearch.h"
#include "EditorTheme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCompleter>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QStandardItemModel>
#include <functional>

namespace Hephaestus {
namespace QtEditor {

namespace {

constexpr int ActionRole = Qt::UserRole + 1;

QString PlainText(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

} // namespace

EditorCommandSearch::EditorCommandSearch(QMenuBar* menuBar, QWidget* parent)
    : QLineEdit(parent), m_menuBar(menuBar)
{
    setObjectName(QStringLiteral("CommandSearch"));
    setPlaceholderText(tr("Search or run a command"));
    setClearButtonEnabled(true);
    setFocusPolicy(Qt::ClickFocus);
    addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);

    m_model = new QStandardItemModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setMaxVisibleItems(12);
    setCompleter(m_completer);

    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            [this](const QModelIndex& index) { runCommand(index); });
}

void EditorCommandSearch::focusInEvent(QFocusEvent* event)
{
    rebuildCommands();
    QLineEdit::focusInEvent(event);
}

void EditorCommandSearch::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    if (!m_completer->popup()->isVisible()) {
        clear();
        if (m_onDismiss) m_onDismiss();
    }
}

void EditorCommandSearch::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        clear();
        clearFocus();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void EditorCommandSearch::rebuildCommands()
{
    m_model->clear();
    if (!m_menuBar) {
        return;
    }
    std::function<void(QMenu*, const QString&)> collect = [&](QMenu* menu, const QString& path) {
        for (QAction* action : menu->actions()) {
            if (action->isSeparator() || !action->isVisible()) {
                continue;
            }
            const QString name = PlainText(action->text());
            if (name.isEmpty()) {
                continue;
            }
            const QString itemPath = path + QStringLiteral(" \u203A ") + name;
            if (QMenu* sub = action->menu()) {
                collect(sub, itemPath);
                continue;
            }
            if (!action->isEnabled()) {
                continue;
            }
            QString label = itemPath;
            if (!action->shortcut().isEmpty()) {
                label += QStringLiteral("    ") + action->shortcut().toString(QKeySequence::NativeText);
            }
            auto* item = new QStandardItem(action->icon(), label);
            item->setData(QVariant::fromValue(QPointer<QAction>(action)), ActionRole);
            m_model->appendRow(item);
        }
    };
    for (QAction* top : m_menuBar->actions()) {
        if (QMenu* menu = top->menu()) {
            collect(menu, PlainText(top->text()));
        }
    }
}

void EditorCommandSearch::runCommand(const QModelIndex& index)
{
    const QPointer<QAction> action = index.data(ActionRole).value<QPointer<QAction>>();
    clear();
    clearFocus();
    if (action && action->isEnabled()) {
        action->trigger();
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
