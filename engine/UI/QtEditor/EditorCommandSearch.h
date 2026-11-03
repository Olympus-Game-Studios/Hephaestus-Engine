#pragma once

/**
 * @file EditorCommandSearch.h
 * @brief Header search field that finds and runs menu commands.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QLineEdit>
#include <QPointer>

class QCompleter;
class QMenuBar;
class QStandardItemModel;

namespace Hephaestus {
namespace QtEditor {

/** Lists every enabled menu-bar command whose path contains the typed text; Enter runs it. */
class EditorCommandSearch final : public QLineEdit
{
public:
    EditorCommandSearch(QMenuBar* menuBar, QWidget* parent);

protected:
    void focusInEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuildCommands();
    void runCommand(const QModelIndex& index);

    QPointer<QMenuBar> m_menuBar;
    QStandardItemModel* m_model = nullptr;
    QCompleter* m_completer = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
