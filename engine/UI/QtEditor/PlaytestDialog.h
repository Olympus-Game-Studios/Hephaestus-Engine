#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Analysis/Playtest.h"

#include <QDialog>

#include <functional>
#include <vector>

QT_BEGIN_NAMESPACE
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
QT_END_NAMESPACE

namespace Hephaestus {
namespace QtEditor {

class QtEditorEngine;

// Runs the open level as a bot match faster than real time and shows where kills, deaths and movement happened as
// heatmaps on the floor.
class PlaytestDialog : public QDialog
{
    Q_OBJECT

public:
    // playStopped runs after the playtest leaves Play, so the editor can refresh its panels.
    PlaytestDialog(QtEditorEngine* engine, std::function<void()> playStopped, QWidget* parent = nullptr);
    ~PlaytestDialog() override;

private:
    void start();
    void step();
    void finish(bool cancelled);
    void showHeatmap();
    void closeEvent(QCloseEvent* event) override;

    QtEditorEngine* m_engine = nullptr;
    std::function<void()> m_playStopped;
    QDoubleSpinBox* m_minutes = nullptr;
    QPushButton* m_run = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_summary = nullptr;
    QComboBox* m_show = nullptr;
    QTimer* m_timer = nullptr;
    float m_simulated = 0.0f;
    float m_target = 0.0f;
    std::vector<Analysis::PlaytestEvent> m_events;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
