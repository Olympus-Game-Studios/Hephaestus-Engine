#if HEPHAESTUS_EDITOR_QT_UI

#include "DebuggerPanel.h"
#include "EditorTheme.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;

QTreeWidget* VariableTree(QWidget* parent)
{
    auto* tree = new QTreeWidget(parent);
    tree->setObjectName(QStringLiteral("DebuggerList"));
    tree->setColumnCount(3);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setTextElideMode(Qt::ElideRight);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree->header()->resizeSection(0, 140);
    return tree;
}

QWidget* Column(const QString& title, QWidget* content, QWidget* parent, QWidget* footer = nullptr)
{
    auto* column = new QWidget(parent);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* heading = new QLabel(title.toUpper(), column);
    heading->setObjectName(QStringLiteral("DebuggerHeading"));
    layout->addWidget(heading);
    layout->addWidget(content, 1);
    if (footer) layout->addWidget(footer);
    return column;
}

void FillVariable(QTreeWidgetItem* item, const LuaDebugger::Variable& variable)
{
    item->setText(1, QString::fromStdString(variable.value));
    item->setText(2, QString::fromStdString(variable.type));
    item->setToolTip(1, item->text(1));
    const bool error = variable.type == "error";
    item->setForeground(1, Theme::Color(error ? Theme::Token::StatusError : Theme::Token::CodeNumber));
    item->setForeground(2, Theme::Color(Theme::Token::TextMuted));
}

} // namespace

DebuggerPanel::DebuggerPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DebuggerPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("ConsoleBar"));
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);
    auto makeButton = [bar, row](const char* icon, const QString& text, Theme::IconTone tone) {
        auto* button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("DebuggerAction"));
        button->setIcon(Theme::Icon(QString::fromLatin1(icon), tone));
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button);
        return button;
    };
    m_pause = makeButton("pause", tr("Pause scripts"), Theme::IconTone::Secondary);
    m_pause->setToolTip(tr("Stop at the next Lua line that runs"));
    connect(m_pause, &QToolButton::clicked, this, [this]() {
        if (onPauseRequested) onPauseRequested();
    });
    row->addStretch(1);
    const struct {
        const char* icon;
        QString text;
        LuaDebugger::Action action;
        Theme::IconTone tone;
    } steps[] = {
        { "play", tr("Continue (F5)"), LuaDebugger::Action::Continue, Theme::IconTone::Success },
        { "step-over", tr("Step over (F10)"), LuaDebugger::Action::StepOver, Theme::IconTone::Secondary },
        { "step-into", tr("Step into (F11)"), LuaDebugger::Action::StepInto, Theme::IconTone::Secondary },
        { "step", tr("Step out (Shift+F11)"), LuaDebugger::Action::StepOut, Theme::IconTone::Secondary },
        { "stop", tr("Stop (Shift+F5)"), LuaDebugger::Action::Stop, Theme::IconTone::Error },
    };
    for (const auto& step : steps) {
        QToolButton* button = makeButton(step.icon, step.text, step.tone);
        const LuaDebugger::Action action = step.action;
        connect(button, &QToolButton::clicked, this, [this, action]() {
            if (onAction) onAction(action);
        });
        m_stepButtons << button;
    }
    layout->addWidget(bar);

    auto* columns = new QSplitter(Qt::Horizontal, this);
    columns->setObjectName(QStringLiteral("DebuggerColumns"));
    m_stack = new QTreeWidget(columns);
    m_stack->setObjectName(QStringLiteral("DebuggerList"));
    m_stack->setColumnCount(2);
    m_stack->setHeaderHidden(true);
    m_stack->setRootIsDecorated(false);
    m_stack->setUniformRowHeights(true);
    m_stack->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stack->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_stack->header()->setStretchLastSection(false);
    connect(m_stack, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item || !m_paused) return;
        refreshVariables();
        const QString path = item->data(0, kPathRole).toString();
        if (!path.isEmpty() && onFrameSelected) onFrameSelected(path, item->data(0, kLineRole).toInt());
    });

    m_watches = VariableTree(columns);
    m_watches->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_watches, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QTreeWidgetItem* item = m_watches->itemAt(position);
        if (!item) return;
        QMenu menu(this);
        menu.addAction(tr("Remove watch"), this, [this, item]() {
            delete item;
            saveWatches();
        });
        menu.exec(m_watches->viewport()->mapToGlobal(position));
    });
    connect(m_watches, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int column) {
        if (column != 0) return;
        saveWatches();
        refreshVariables();
    });
    m_addWatch = new QLineEdit(columns);
    m_addWatch->setObjectName(QStringLiteral("DebuggerAddWatch"));
    m_addWatch->setPlaceholderText(tr("Add watch"));
    m_addWatch->addAction(Theme::Icon(QStringLiteral("plus"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
    connect(m_addWatch, &QLineEdit::returnPressed, this, [this]() {
        if (m_addWatch->text().trimmed().isEmpty()) return;
        addWatch(m_addWatch->text().trimmed());
        m_addWatch->clear();
        saveWatches();
        refreshVariables();
    });

    m_locals = VariableTree(columns);
    columns->addWidget(Column(tr("Call stack"), m_stack, columns));
    columns->addWidget(Column(tr("Watch"), m_watches, columns, m_addWatch));
    columns->addWidget(Column(tr("Locals"), m_locals, columns));
    layout->addWidget(columns, 1);

    const QStringList watches = QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"))
                                    .value(QStringLiteral("debugger/watches")).toStringList();
    for (const QString& expression : watches) addWatch(expression);
    ShowRunning(false);
}

void DebuggerPanel::addWatch(const QString& expression)
{
    const QSignalBlocker block(m_watches);
    auto* item = new QTreeWidgetItem(m_watches, { expression, QStringLiteral("—"), QString() });
    item->setForeground(2, Theme::Color(Theme::Token::TextMuted));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

void DebuggerPanel::saveWatches() const
{
    QStringList expressions;
    for (int i = 0; i < m_watches->topLevelItemCount(); ++i) expressions << m_watches->topLevelItem(i)->text(0);
    QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor")).setValue(QStringLiteral("debugger/watches"), expressions);
}

void DebuggerPanel::ShowPause(const LuaDebugger::PauseState& state)
{
    m_state = state;
    m_paused = true;
    m_pause->setEnabled(false);
    for (QToolButton* button : m_stepButtons) button->setEnabled(true);
    m_stack->clear();
    for (std::size_t i = 0; i < state.frames.size(); ++i) {
        const LuaDebugger::Frame& frame = state.frames[i];
        const QString path = QString::fromStdString(frame.source);
        auto* item = new QTreeWidgetItem(m_stack, {
            QString::fromStdString(frame.function),
            frame.line > 0 ? QStringLiteral("%1:%2").arg(QFileInfo(path).fileName()).arg(frame.line) : QString() });
        item->setIcon(0, i == 0 ? Theme::Icon(QStringLiteral("play"), Theme::IconTone::Warning) : QIcon());
        item->setForeground(1, Theme::Color(Theme::Token::TextMuted));
        item->setToolTip(1, path);
        item->setData(0, kPathRole, frame.line > 0 ? path : QString());
        item->setData(0, kLineRole, frame.line);
    }
    {
        const QSignalBlocker block(m_stack);
        if (m_stack->topLevelItemCount() > 0) m_stack->setCurrentItem(m_stack->topLevelItem(0));
    }
    refreshVariables();
}

void DebuggerPanel::ShowRunning(bool playing)
{
    m_paused = false;
    m_pause->setEnabled(playing);
    for (QToolButton* button : m_stepButtons) button->setEnabled(false);
    m_stack->clear();
    m_locals->clear();
    const QSignalBlocker block(m_watches);
    for (int i = 0; i < m_watches->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_watches->topLevelItem(i);
        item->setText(1, QStringLiteral("—"));
        item->setText(2, QString());
        item->setForeground(1, Theme::Color(Theme::Token::TextMuted));
    }
}

int DebuggerPanel::SelectedFrame() const
{
    return m_stack->currentItem() ? m_stack->indexOfTopLevelItem(m_stack->currentItem()) : 0;
}

void DebuggerPanel::refreshVariables()
{
    if (!m_paused) return;
    const int frame = SelectedFrame();
    const QSignalBlocker block(m_watches);
    m_locals->clear();
    for (const LuaDebugger::Variable& variable : LuaDebugger::Instance().Locals(frame)) {
        auto* item = new QTreeWidgetItem(m_locals, { QString::fromStdString(variable.name) });
        FillVariable(item, variable);
    }
    for (int i = 0; i < m_watches->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_watches->topLevelItem(i);
        FillVariable(item, LuaDebugger::Instance().Evaluate(item->text(0).toStdString(), frame));
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
