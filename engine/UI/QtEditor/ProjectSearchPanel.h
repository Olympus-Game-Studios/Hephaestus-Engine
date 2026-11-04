#pragma once

/**
 * @file ProjectSearchPanel.h
 * @brief Searches the project's script and text files and lists matching lines.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QToolButton;
class QTreeWidget;

namespace Hephaestus {
namespace QtEditor {

class ProjectSearchPanel final : public QWidget
{
public:
    explicit ProjectSearchPanel(QWidget* parent = nullptr);

    /** Focuses the search field, filled with @p text when it is not empty. */
    void FocusSearch(const QString& text = {});

    /** Runs the search over every searchable file under the project root. */
    void Search();

    std::function<void(const QString& path, int line, int column)> onOpen;

private:
    QLineEdit* m_query = nullptr;
    QToolButton* m_matchCase = nullptr;
    QToolButton* m_wholeWord = nullptr;
    QToolButton* m_useRegex = nullptr;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_results = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
