#if HEPHAESTUS_EDITOR_QT_UI

#include "PlaytestDialog.h"

#include "QtEditorEngine.h"
#include "../../Core/EditorContext.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

namespace {
constexpr float kFrameSeconds = 1.0f / 60.0f;
constexpr std::uint32_t kFramesPerStep = 60;
}

PlaytestDialog::PlaytestDialog(QtEditorEngine* engine, std::function<void()> playStopped, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_playStopped(std::move(playStopped))
{
    setWindowTitle(tr("Bot Playtest"));
    resize(520, 300);
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Plays the open level as a bot match, faster than real time, and colours the floor by "
                                "where soldiers walked, killed and died. Needs a team deathmatch script in the level; "
                                "you sit out, and bots fill both teams."));
    intro->setWordWrap(true);
    root->addWidget(intro);
    auto* form = new QFormLayout();
    m_minutes = new QDoubleSpinBox();
    m_minutes->setRange(0.5, 60.0);
    m_minutes->setSingleStep(1.0);
    m_minutes->setValue(5.0);
    m_minutes->setSuffix(tr(" min"));
    m_minutes->setToolTip(tr("Match time to simulate. Longer runs give smoother heatmaps."));
    form->addRow(tr("Length"), m_minutes);
    m_show = new QComboBox();
    m_show->addItem(tr("Traffic"), QStringLiteral("traffic"));
    m_show->addItem(tr("Kills (where the shooter stood)"), QStringLiteral("kill"));
    m_show->addItem(tr("Deaths"), QStringLiteral("death"));
    m_show->addItem(tr("Off"), QString());
    m_show->setEnabled(false);
    form->addRow(tr("Show"), m_show);
    root->addLayout(form);
    m_progress = new QProgressBar();
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    root->addWidget(m_progress);
    m_summary = new QLabel();
    m_summary->setWordWrap(true);
    root->addWidget(m_summary, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    m_run = buttons->addButton(tr("Run"), QDialogButtonBox::ActionRole);
    root->addWidget(buttons);
    m_timer = new QTimer(this);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, &PlaytestDialog::step);
    connect(m_run, &QPushButton::clicked, this, [this]() {
        if (m_timer->isActive()) {
            finish(true);
        } else {
            start();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_show, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { showHeatmap(); });
}

PlaytestDialog::~PlaytestDialog()
{
    if (m_engine) m_engine->SetHeatmapLines({});
}

void PlaytestDialog::start()
{
    auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
    if (!ctx || !ctx->HasActiveScene()) {
        m_summary->setText(tr("Open a level first."));
        return;
    }
    if (m_engine->IsPlaying()) {
        m_summary->setText(tr("Stop Play first; the playtest starts its own."));
        return;
    }
    m_engine->SetHeatmapLines({});
    m_events.clear();
    m_simulated = 0.0f;
    m_target = static_cast<float>(m_minutes->value()) * 60.0f;
    Analysis::BeginPlaytest();
    m_engine->StartPlayMode();
    if (!m_engine->IsPlaying()) {
        Analysis::EndPlaytest();
        m_summary->setText(tr("Play Mode did not start."));
        return;
    }
    m_engine->SetRealtime(false);
    m_run->setText(tr("Stop"));
    m_minutes->setEnabled(false);
    m_show->setEnabled(false);
    m_progress->setValue(0);
    m_summary->setText(tr("Playing..."));
    m_timer->start();
}

void PlaytestDialog::step()
{
    std::string error;
    if (!m_engine->IsPlaying() || !m_engine->StepFrames(kFramesPerStep, kFrameSeconds, error)) {
        m_summary->setText(tr("The playtest stopped early: %1").arg(QString::fromStdString(error)));
        finish(true);
        return;
    }
    const float seconds = kFramesPerStep * kFrameSeconds;
    m_simulated += seconds;
    m_progress->setValue(static_cast<int>(1000.0f * m_simulated / m_target));
    if (m_simulated >= m_target) {
        finish(false);
    }
}

void PlaytestDialog::finish(bool cancelled)
{
    m_timer->stop();
    m_events = Analysis::EndPlaytest();
    if (m_engine->IsPlaying()) {
        m_engine->SetRealtime(true);
        m_engine->StopPlayMode();
        if (m_playStopped) m_playStopped();
    }
    m_run->setText(tr("Run"));
    m_minutes->setEnabled(true);
    int counts[3] = {0, 0, 0};
    int killsByTeam[3] = {0, 0, 0};
    for (const Analysis::PlaytestEvent& event : m_events) {
        if (event.kind == "kill") {
            ++counts[0];
            if (event.team == 1 || event.team == 2) ++killsByTeam[event.team];
        }
        counts[1] += event.kind == "death" ? 1 : 0;
        counts[2] += event.kind == "traffic" ? 1 : 0;
    }
    QString summary = (cancelled ? tr("Stopped after %1 min of simulated play. ")
                                 : tr("%1 min of simulated play. ")).arg(m_simulated / 60.0f, 0, 'f', 1);
    if (counts[2] == 0) {
        summary += tr("Nothing was recorded: the level needs a team deathmatch script (Hephaestus FPS plugin) with "
                      "spawn points and soldier prefabs.");
    } else {
        summary += tr("%1 kills (team 1: %2, team 2: %3), %4 deaths, %5 position samples.")
                       .arg(counts[0]).arg(killsByTeam[1]).arg(killsByTeam[2]).arg(counts[1]).arg(counts[2]);
        const Analysis::Heatmap kills = Analysis::BuildHeatmap(m_events, "kill", 4.0f);
        if (!kills.cells.empty()) {
            const Analysis::HeatmapCell& hot = kills.cells.front();
            summary += tr(" The busiest fighting spot, around (%1, %2), saw %3% of the kills.")
                           .arg((hot.x + 0.5f) * kills.cellSize, 0, 'f', 0)
                           .arg((hot.z + 0.5f) * kills.cellSize, 0, 'f', 0)
                           .arg(100.0f * hot.count / std::max(1, kills.total), 0, 'f', 0);
        }
    }
    m_summary->setText(summary);
    m_show->setEnabled(counts[2] > 0);
    showHeatmap();
}

void PlaytestDialog::showHeatmap()
{
    const std::string kind = m_show->currentData().toString().toStdString();
    std::vector<EditorDebugLine> lines;
    if (!kind.empty() && !m_events.empty()) {
        Analysis::AppendHeatmapLines(Analysis::BuildHeatmap(m_events, kind, kind == "traffic" ? 2.0f : 4.0f), lines);
    }
    m_engine->SetHeatmapLines(std::move(lines));
}

void PlaytestDialog::closeEvent(QCloseEvent* event)
{
    if (m_timer->isActive()) {
        finish(true);
    }
    QDialog::closeEvent(event);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
