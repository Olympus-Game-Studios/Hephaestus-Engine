#pragma once

/**
 * @file EditorDockTitleBar.h
 * @brief Editor 2.0 panel header: title, panel actions and a panel menu.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QList>

class QDockWidget;
class QWidget;

namespace Hephaestus {
namespace QtEditor {

/**
 * Replaces @p dock's title bar with the panel header. @p actions (owned by the
 * caller, usually tool buttons) sit before the panel menu, which offers Float
 * and Close.
 */
void InstallDockTitleBar(QDockWidget* dock, const QList<QWidget*>& actions = {});

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
