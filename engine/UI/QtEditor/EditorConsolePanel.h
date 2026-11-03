#pragma once

/**
 * @file EditorConsolePanel.h
 * @brief Console panel listing engine log entries with level and text filters.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <cstdint>

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
    std::uint64_t m_cursor = 0;
    int m_infoCount = 0;
    int m_warningCount = 0;
    int m_errorCount = 0;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
