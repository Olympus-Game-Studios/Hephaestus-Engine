#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Analysis/LevelChecks.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace Hephaestus {
namespace QtEditor {

class QtEditorEngine;

// Lists what in the open level the characters can't get through, climb or reach. Double-clicking a problem selects
// the shape at fault and frames it in the viewport.
class LevelCheckDialog : public QDialog
{
    Q_OBJECT

public:
    LevelCheckDialog(QtEditorEngine* engine, QWidget* parent = nullptr);

    void RunCheck();

private:
    void showProblem(QTreeWidgetItem* item);

    QtEditorEngine* m_engine = nullptr;
    Analysis::LevelCheckReport m_report;
    QLabel* m_metrics = nullptr;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_list = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
