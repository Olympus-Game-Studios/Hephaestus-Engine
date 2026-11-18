#if HEPHAESTUS_EDITOR_QT_UI

#include "LevelCheckDialog.h"

#include "EditorTheme.h"
#include "QtEditorEngine.h"
#include "../../Assets/AssetManager.h"
#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

LevelCheckDialog::LevelCheckDialog(QtEditorEngine* engine, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(tr("Check Level"));
    resize(720, 420);
    auto* root = new QVBoxLayout(this);
    m_metrics = new QLabel();
    m_metrics->setWordWrap(true);
    root->addWidget(m_metrics);
    m_summary = new QLabel();
    root->addWidget(m_summary);
    m_list = new QTreeWidget();
    m_list->setColumnCount(1);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setWordWrap(true);
    m_list->setToolTip(tr("Double-click a problem to select it and frame it in the viewport."));
    root->addWidget(m_list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton* again = buttons->addButton(tr("Check Again"), QDialogButtonBox::ActionRole);
    root->addWidget(buttons);
    connect(again, &QPushButton::clicked, this, &LevelCheckDialog::RunCheck);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { showProblem(item); });
}

void LevelCheckDialog::RunCheck()
{
    m_list->clear();
    auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
    if (!ctx || !ctx->HasActiveScene()) {
        m_metrics->setText(QString());
        m_summary->setText(tr("Open a scene to check it."));
        return;
    }
    m_report = Analysis::CheckLevel(*ctx->GetActiveScene(), AssetManager::GetInstance().GetProjectRoot());
    const Analysis::TraversalMetrics& m = m_report.metrics;
    QString metrics = tr("Checked for characters %1 m tall and %2 m wide, climbing steps up to %3 m and slopes up to "
                         "%4 degrees")
                          .arg(m.height, 0, 'f', 2)
                          .arg(m.radius * 2.0f, 0, 'f', 2)
                          .arg(m.stepHeight, 0, 'f', 2)
                          .arg(m.maxSlopeDegrees, 0, 'f', 0);
    metrics += m.fromControllers ? tr(", jumping %1 m.").arg(m.jumpHeight, 0, 'f', 2)
                                 : tr(" (the default player; no character is in the scene).");
    m_metrics->setText(metrics);
    int errors = 0;
    int warnings = 0;
    for (size_t index = 0; index < m_report.problems.size(); ++index) {
        const Analysis::LevelProblem& problem = m_report.problems[index];
        auto* item = new QTreeWidgetItem(m_list, {QString::fromStdString(problem.message)});
        item->setData(0, Qt::UserRole, static_cast<int>(index));
        item->setToolTip(0, QString::fromStdString(problem.check));
        switch (problem.severity) {
            case Analysis::Severity::Error:
                item->setIcon(0, Theme::Icon(QStringLiteral("error"), Theme::IconTone::Error));
                ++errors;
                break;
            case Analysis::Severity::Warning:
                item->setIcon(0, Theme::Icon(QStringLiteral("warning"), Theme::IconTone::Warning));
                ++warnings;
                break;
            case Analysis::Severity::Info:
                item->setIcon(0, Theme::Icon(QStringLiteral("info"), Theme::IconTone::Info));
                break;
        }
    }
    m_summary->setText(errors + warnings == 0
        ? tr("No problems found.")
        : tr("%n error(s)", nullptr, errors) + tr(", %n warning(s)", nullptr, warnings));
}

void LevelCheckDialog::showProblem(QTreeWidgetItem* item)
{
    const int index = item ? item->data(0, Qt::UserRole).toInt() : -1;
    auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
    if (index < 0 || index >= static_cast<int>(m_report.problems.size()) || !ctx || !ctx->HasActiveScene()) {
        return;
    }
    const Analysis::LevelProblem& problem = m_report.problems[static_cast<size_t>(index)];
    if (problem.entity != entt::null && ctx->GetActiveScene()->GetRegistry().valid(problem.entity)) {
        ctx->SelectEntity(problem.entity);
    }
    m_engine->GetCameraController()->FocusOnPoint(problem.position.x, problem.position.y, problem.position.z, 2.5f);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
