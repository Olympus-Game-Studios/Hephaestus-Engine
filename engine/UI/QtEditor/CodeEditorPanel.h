#pragma once

/**
 * @file CodeEditorPanel.h
 * @brief Programmer-mode code editor: tabbed text documents with line numbers
 * and syntax highlighting supplied by the script language registry.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QHash>
#include <QList>
#include <QPair>
#include <QWidget>

#include <functional>

class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace Hephaestus {
namespace QtEditor {

class CodeEditor;

class CodeEditorPanel final : public QWidget
{
public:
    explicit CodeEditorPanel(QWidget* parent = nullptr);

    /** Opens @p absolutePath in a tab, or focuses the tab already showing it. */
    bool OpenFile(const QString& absolutePath);

    /** Opens @p absolutePath and puts the cursor at the start of 1-based @p line. */
    bool OpenFileAt(const QString& absolutePath, int line);

    /** Asks to save modified documents, then closes every tab. False when cancelled. */
    bool CloseAll();

    /**
     * Underlines 1-based @p line of @p absolutePath as an error, now or when the
     * file is next opened. Marks clear when the file is saved.
     */
    void MarkError(const QString& absolutePath, int line, const QString& message);

    /**
     * Turns a file name as a log message prints it (absolute, project-relative,
     * or a path tail such as Lua's "...scripts/door.lua") into an existing
     * absolute path inside the project. Empty when no file matches.
     */
    static QString ResolveLocation(const QString& location);

    /** Opens the file where the debugger stopped and marks @p line; an empty path clears the mark. */
    void ShowExecutionLine(const QString& absolutePath, int line);

    /** Remembers the open tabs, cursors and breakpoints for the current project. */
    void SaveSession();

    /** Reopens the current project's remembered tabs and restores its breakpoints. */
    void RestoreSession();

    /** Called with "Ln 3, Col 7  ·  UTF-8  ·  LF  ·  Lua", or empty when no file is shown. */
    std::function<void(const QString&)> onStatusChanged;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    CodeEditor* editorAt(int index) const;
    CodeEditor* currentEditor() const;
    CodeEditor* editorForPath(const QString& path) const;
    bool save(CodeEditor* editor);
    bool closeTab(int index);
    void updateTab(CodeEditor* editor);
    void showCurrentTab();
    void updateStatus();

    void showFind(bool replace);
    void hideFind();
    void runSearch(bool moveCursor);
    void findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void showGoToLine();

    void onFileChanged(const QString& path);
    void reloadFromDisk(CodeEditor* editor);
    void updateBanner();
    void syncBreakpoints(CodeEditor* editor);

    QTabBar* m_tabs = nullptr;
    QLabel* m_breadcrumbs = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_emptyState = nullptr;

    QWidget* m_findBar = nullptr;
    QLineEdit* m_findField = nullptr;
    QLabel* m_findCount = nullptr;
    QToolButton* m_matchCase = nullptr;
    QToolButton* m_wholeWord = nullptr;
    QToolButton* m_useRegex = nullptr;
    QWidget* m_replaceRow = nullptr;
    QLineEdit* m_replaceField = nullptr;

    QWidget* m_goToBar = nullptr;
    QLineEdit* m_goToField = nullptr;

    QWidget* m_banner = nullptr;
    QLabel* m_bannerText = nullptr;
    QPushButton* m_bannerReload = nullptr;

    QFileSystemWatcher* m_watcher = nullptr;
    QHash<QString, QList<QPair<int, QString>>> m_pendingErrors;
    QHash<QString, QList<int>> m_breakpoints;
    QString m_executionPath;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
