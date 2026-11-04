#pragma once

/**
 * @file DebuggerPanel.h
 * @brief Lua debugger controls: call stack, watches and locals while paused.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Scripting/LuaDebugger.h"

#include <QWidget>

#include <functional>

class QLineEdit;
class QToolButton;
class QTreeWidget;

namespace Hephaestus {
namespace QtEditor {

class DebuggerPanel final : public QWidget
{
public:
    explicit DebuggerPanel(QWidget* parent = nullptr);

    /** Lists the stopped frames and selects the top one. */
    void ShowPause(const LuaDebugger::PauseState& state);

    /** Clears the stopped state; @p playing enables Pause. */
    void ShowRunning(bool playing);

    int SelectedFrame() const;

    std::function<void(LuaDebugger::Action)> onAction;
    std::function<void()> onPauseRequested;
    std::function<void(const QString& path, int line)> onFrameSelected;

private:
    void refreshVariables();
    void addWatch(const QString& expression);
    void saveWatches() const;

    QToolButton* m_pause = nullptr;
    QList<QToolButton*> m_stepButtons;
    QTreeWidget* m_stack = nullptr;
    QTreeWidget* m_watches = nullptr;
    QTreeWidget* m_locals = nullptr;
    QLineEdit* m_addWatch = nullptr;
    LuaDebugger::PauseState m_state;
    bool m_paused = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
