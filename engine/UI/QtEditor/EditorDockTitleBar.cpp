#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorDockTitleBar.h"
#include "EditorTheme.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

namespace Hephaestus {
namespace QtEditor {

namespace {

/** A docked panel in a tab group is labelled by its tab, so its header collapses. */
void SyncHeaderWithTabs(QDockWidget* dock, QWidget* header, QWidget* collapsed)
{
    auto* window = qobject_cast<QMainWindow*>(dock->parentWidget());
    const bool tabbed = window && !dock->isFloating() && !window->tabifiedDockWidgets(dock).isEmpty();
    QWidget* wanted = tabbed ? collapsed : header;
    if (dock->titleBarWidget() != wanted) {
        dock->setTitleBarWidget(wanted);
        (tabbed ? header : collapsed)->hide();
        wanted->show();
    }
}

} // namespace

QWidget* CreatePanelHeader(const QString& title, QWidget* parent, const QList<QWidget*>& actions)
{
    auto* header = new QWidget(parent);
    header->setObjectName(QStringLiteral("PanelHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(12, 0, 6, 0);
    layout->setSpacing(2);

    auto* titleLabel = new QLabel(title, header);
    titleLabel->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(titleLabel);
    layout->addStretch(1);

    for (QWidget* action : actions) {
        action->setParent(header);
        layout->addWidget(action);
    }
    return header;
}

void InstallDockTitleBar(QDockWidget* dock, const QList<QWidget*>& actions)
{
    QWidget* header = CreatePanelHeader(dock->windowTitle(), dock, actions);
    auto* layout = static_cast<QHBoxLayout*>(header->layout());
    auto* title = header->findChild<QLabel*>(QStringLiteral("PanelTitle"));
    QObject::connect(dock, &QDockWidget::windowTitleChanged, title, &QLabel::setText);

    auto* more = new QToolButton(header);
    more->setObjectName(QStringLiteral("PanelMenuButton"));
    more->setIcon(Theme::Icon(QStringLiteral("more")));
    more->setToolTip(QObject::tr("Panel options"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more);
    QAction* floatAction = menu->addAction(QObject::tr("Float"));
    QObject::connect(floatAction, &QAction::triggered, dock, [dock]() { dock->setFloating(!dock->isFloating()); });
    QObject::connect(menu, &QMenu::aboutToShow, dock, [dock, floatAction]() {
        floatAction->setText(dock->isFloating() ? QObject::tr("Dock") : QObject::tr("Float"));
    });
    menu->addAction(QObject::tr("Close"), dock, &QDockWidget::close);
    more->setMenu(menu);
    layout->addWidget(more);

    auto* collapsed = new QWidget(dock);
    collapsed->setFixedHeight(0);
    collapsed->hide();

    dock->setTitleBarWidget(header);
    // Tabbing changes without a dedicated signal; these cover docking, floating and tab activation.
    auto sync = [dock, header, collapsed]() {
        QTimer::singleShot(0, dock, [dock, header, collapsed]() { SyncHeaderWithTabs(dock, header, collapsed); });
    };
    QObject::connect(dock, &QDockWidget::dockLocationChanged, dock, sync);
    QObject::connect(dock, &QDockWidget::topLevelChanged, dock, sync);
    QObject::connect(dock, &QDockWidget::visibilityChanged, dock, sync);
    sync();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
