#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorConsolePanel.h"
#include "EditorTheme.h"
#include "../../Core/EngineLog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

namespace {

constexpr int kMaxRows = 5000;
constexpr int kLevelRole = Qt::UserRole + 1;

enum class Severity { Info, Warning, Error };

Severity SeverityOf(EngineLogLevel level)
{
    switch (level) {
    case EngineLogLevel::Warning: return Severity::Warning;
    case EngineLogLevel::Error:
    case EngineLogLevel::Fatal:   return Severity::Error;
    default:                      return Severity::Info;
    }
}

} // namespace

EditorConsolePanel::EditorConsolePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ConsolePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("ConsoleBar"));
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Filter messages..."));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
    m_search->setMaximumWidth(280);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilters(); });
    row->addWidget(m_search);
    row->addStretch(1);

    auto makeLevel = [this, bar, row](const char* icon, Theme::IconTone tone, const char* level) {
        auto* button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("ConsoleLevel"));
        button->setProperty("level", QString::fromLatin1(level));
        button->setIcon(Theme::Icon(QString::fromLatin1(icon), tone));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        button->setChecked(true);
        connect(button, &QToolButton::toggled, this, [this]() { applyFilters(); });
        row->addWidget(button);
        return button;
    };
    m_info = makeLevel("info", Theme::IconTone::Info, "info");
    m_warnings = makeLevel("warning", Theme::IconTone::Warning, "warning");
    m_errors = makeLevel("error", Theme::IconTone::Error, "error");

    auto* clear = new QToolButton(bar);
    clear->setIcon(Theme::Icon(QStringLiteral("close")));
    clear->setToolTip(tr("Clear the console"));
    connect(clear, &QToolButton::clicked, this, [this]() {
        m_list->clear();
        m_infoCount = m_warningCount = m_errorCount = 0;
        updateCounts();
    });
    row->addWidget(clear);
    layout->addWidget(bar);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("ConsoleList"));
    m_list->setColumnCount(3);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setStretchLastSection(true);
    layout->addWidget(m_list, 1);

    updateCounts();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &EditorConsolePanel::poll);
    m_timer->start(150);
    poll();
}

void EditorConsolePanel::poll()
{
    for (;;) {
        const EngineLogSnapshot snapshot = EngineLog::Snapshot(m_cursor, 500);
        if (snapshot.entries.empty()) {
            break;
        }
        for (const EngineLogEntry& entry : snapshot.entries) {
            append(entry);
        }
        m_cursor = snapshot.nextCursor;
    }
}

void EditorConsolePanel::append(const EngineLogEntry& entry)
{
    const Severity severity = SeverityOf(entry.level);
    const bool atBottom = m_list->verticalScrollBar()->value() == m_list->verticalScrollBar()->maximum();

    auto* item = new QTreeWidgetItem();
    QString time = QString::fromStdString(entry.timestamp);
    const int space = time.lastIndexOf(QLatin1Char(' '));
    if (space >= 0) time = time.mid(space + 1);
    item->setText(0, time);
    item->setText(1, QString::fromStdString(entry.category).toUpper());
    item->setText(2, QString::fromStdString(entry.message).trimmed());
    item->setToolTip(2, item->text(2));
    item->setData(0, kLevelRole, static_cast<int>(severity));
    item->setForeground(0, Theme::Color(Theme::Token::TextMuted));
    item->setForeground(1, Theme::Color(Theme::Token::TextSecondary));
    switch (severity) {
    case Severity::Info:
        item->setIcon(0, Theme::Icon(QStringLiteral("info"), Theme::IconTone::Info));
        ++m_infoCount;
        break;
    case Severity::Warning:
        item->setIcon(0, Theme::Icon(QStringLiteral("warning"), Theme::IconTone::Warning));
        for (int c = 0; c < 3; ++c) item->setBackground(c, Theme::Color(Theme::Token::StatusWarningBg));
        ++m_warningCount;
        break;
    case Severity::Error:
        item->setIcon(0, Theme::Icon(QStringLiteral("error"), Theme::IconTone::Error));
        for (int c = 0; c < 3; ++c) item->setBackground(c, Theme::Color(Theme::Token::StatusErrorBg));
        ++m_errorCount;
        break;
    }
    m_list->addTopLevelItem(item);
    applyFilter(item);
    while (m_list->topLevelItemCount() > kMaxRows) {
        delete m_list->takeTopLevelItem(0);
    }
    updateCounts();
    if (atBottom) {
        m_list->scrollToBottom();
    }
}

void EditorConsolePanel::applyFilter(QTreeWidgetItem* item) const
{
    const auto severity = static_cast<Severity>(item->data(0, kLevelRole).toInt());
    const bool levelShown = (severity == Severity::Info && m_info->isChecked()) ||
                            (severity == Severity::Warning && m_warnings->isChecked()) ||
                            (severity == Severity::Error && m_errors->isChecked());
    const QString text = m_search->text().trimmed();
    const bool textShown = text.isEmpty() || item->text(2).contains(text, Qt::CaseInsensitive) ||
                           item->text(1).contains(text, Qt::CaseInsensitive);
    item->setHidden(!(levelShown && textShown));
}

void EditorConsolePanel::applyFilters()
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        applyFilter(m_list->topLevelItem(i));
    }
}

void EditorConsolePanel::updateCounts()
{
    m_info->setText(tr("Info %1").arg(m_infoCount));
    m_warnings->setText(tr("Warnings %1").arg(m_warningCount));
    m_errors->setText(tr("Errors %1").arg(m_errorCount));
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
