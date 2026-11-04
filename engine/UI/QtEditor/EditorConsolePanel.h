#pragma once

/**
 * @file EditorConsolePanel.h
 * @brief Console panel listing engine log entries with level and text filters.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QStringList>
#include <QWidget>
#include <cstdint>
#include <functional>

class QLineEdit;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace Hephaestus {
struct EngineLogEntry;

namespace QtEditor {

class EditorConsolePanel final : public QWidget
{
public:
    explicit EditorConsolePanel(QWidget* parent = nullptr);

    /** Double-clicked entry naming a file as "path:line". */
    std::function<void(const QString& location, int line)> onOpenLocation;
    /** Error entry naming a file as "path:line", with the message. */
    std::function<void(const QString& location, int line, const QString& message)> onErrorLocation;
    /** Text entered at the "lua>" prompt. */
    std::function<void(const QString& code)> onSubmit;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void poll();
    void append(const EngineLogEntry& entry);
    void applyFilter(QTreeWidgetItem* item) const;
    void applyFilters();
    void updateCounts();

    QTreeWidget* m_list = nullptr;
    QLineEdit* m_search = nullptr;
    QToolButton* m_info = nullptr;
    QToolButton* m_warnings = nullptr;
    QToolButton* m_errors = nullptr;
    QTimer* m_timer = nullptr;
    QLineEdit* m_input = nullptr;
    QStringList m_history;
    int m_historyIndex = 0;
    std::uint64_t m_cursor = 0;
    int m_infoCount = 0;
    int m_warningCount = 0;
    int m_errorCount = 0;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
