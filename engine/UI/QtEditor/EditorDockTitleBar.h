#pragma once

/**
 * @file EditorDockTitleBar.h
 * @brief Editor 2.0 panel header: title, panel actions and a panel menu.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QList>

class QDockWidget;
class QString;
class QWidget;

namespace Hephaestus {
namespace QtEditor {

/**
 * Panel header (title plus @p actions) for panels that are not docks, such as
 * the panes of an asset editor window.
 */
QWidget* CreatePanelHeader(const QString& title, QWidget* parent, const QList<QWidget*>& actions = {});

/**
 * Replaces @p dock's title bar with the panel header. @p actions (owned by the
 * caller, usually tool buttons) sit before the panel menu, which offers Float
 * and Close.
 */
void InstallDockTitleBar(QDockWidget* dock, const QList<QWidget*>& actions = {});

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
