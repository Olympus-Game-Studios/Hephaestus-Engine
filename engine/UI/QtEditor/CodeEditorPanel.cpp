#if HEPHAESTUS_EDITOR_QT_UI

#include "CodeEditorPanel.h"
#include "EditorTheme.h"
#include "../../Assets/AssetManager.h"
#include "../../Core/EngineLog.h"
#include "../../Scripting/ScriptLanguageRegistry.h"
#include "../../Scripting/LuaDebugger.h"

#include <QDir>
#include <QSet>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QStandardItemModel>
#include <QSettings>
#include <QCryptographicHash>
#include <QCompleter>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QSyntaxHighlighter>
#include <QTabBar>
#include <QTextBlock>
#include <QTextLayout>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <climits>
#include <functional>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

namespace {

Theme::Token TokenFor(uint32_t style)
{
    switch (style) {
    case HP_SYNTAX_KEYWORD: return Theme::Token::CodeKeyword;
    case HP_SYNTAX_TYPE: return Theme::Token::CodeType;
    case HP_SYNTAX_FUNCTION: return Theme::Token::CodeFunction;
    case HP_SYNTAX_STRING: return Theme::Token::CodeString;
    case HP_SYNTAX_NUMBER: return Theme::Token::CodeNumber;
    case HP_SYNTAX_COMMENT: return Theme::Token::CodeComment;
    default: return Theme::Token::CodePlain;
    }
}

QRegularExpression CompilePattern(const char* pattern, const QString& path)
{
    QRegularExpression expression(QString::fromUtf8(pattern ? pattern : ""));
    if (!pattern || !*pattern || !expression.isValid()) {
        EngineLog::Warn("[CodeEditor] Ignoring invalid syntax pattern '" + std::string(pattern ? pattern : "") +
                        "' for " + path.toStdString() + ": " + expression.errorString().toStdString());
        return {};
    }
    return expression;
}

class SyntaxHighlighter final : public QSyntaxHighlighter
{
public:
    SyntaxHighlighter(const HPSyntax& syntax, const QString& path, QTextDocument* document)
        : QSyntaxHighlighter(document)
    {
        for (uint32_t i = 0; syntax.rules && i < syntax.ruleCount; ++i) {
            QRegularExpression pattern = CompilePattern(syntax.rules[i].pattern, path);
            if (!pattern.pattern().isEmpty()) {
                m_rules.push_back({ std::move(pattern), {}, format(syntax.rules[i].style) });
            }
        }
        for (uint32_t i = 0; syntax.blocks && i < syntax.blockCount; ++i) {
            QRegularExpression start = CompilePattern(syntax.blocks[i].startPattern, path);
            QRegularExpression end = CompilePattern(syntax.blocks[i].endPattern, path);
            if (!start.pattern().isEmpty() && !end.pattern().isEmpty()) {
                m_blocks.push_back({ std::move(start), std::move(end), format(syntax.blocks[i].style) });
            }
        }
    }

protected:
    void highlightBlock(const QString& text) override
    {
        setCurrentBlockState(-1);
        qsizetype position = 0;
        const int openBlock = previousBlockState();
        if (openBlock >= 0 && openBlock < static_cast<int>(m_blocks.size())
            && !closeBlock(text, openBlock, 0, position)) {
            return;
        }

        while (position < text.size()) {
            const Pattern* best = nullptr;
            QRegularExpressionMatch bestMatch;
            auto consider = [&](const Pattern& candidate) {
                const QRegularExpressionMatch match = candidate.start.match(text, position);
                if (match.hasMatch() && match.capturedLength() > 0
                    && (!best || match.capturedStart() < bestMatch.capturedStart())) {
                    best = &candidate;
                    bestMatch = match;
                }
            };
            for (const Pattern& rule : m_rules) consider(rule);
            for (const Pattern& block : m_blocks) consider(block);
            if (!best) return;

            if (best->end.pattern().isEmpty()) {
                setFormat(bestMatch.capturedStart(), bestMatch.capturedLength(), best->format);
                position = bestMatch.capturedEnd();
                continue;
            }
            const int blockIndex = static_cast<int>(best - m_blocks.data());
            setFormat(bestMatch.capturedStart(), bestMatch.capturedLength(), best->format);
            position = bestMatch.capturedEnd();
            if (!closeBlock(text, blockIndex, bestMatch.capturedStart(), position)) return;
        }
    }

private:
    struct Pattern
    {
        QRegularExpression start;
        QRegularExpression end;
        QTextCharFormat format;
    };

    static QTextCharFormat format(uint32_t style)
    {
        QTextCharFormat result;
        result.setForeground(Theme::Color(TokenFor(style)));
        return result;
    }

    // Colours from spanStart to the block's end pattern; false when the block runs past this line.
    bool closeBlock(const QString& text, int blockIndex, qsizetype spanStart, qsizetype& position)
    {
        const Pattern& block = m_blocks[blockIndex];
        const QRegularExpressionMatch end = block.end.match(text, position);
        if (!end.hasMatch()) {
            setFormat(spanStart, text.size() - spanStart, block.format);
            setCurrentBlockState(blockIndex);
            return false;
        }
        setFormat(spanStart, end.capturedEnd() - spanStart, block.format);
        position = end.capturedEnd();
        return true;
    }

    std::vector<Pattern> m_rules;
    std::vector<Pattern> m_blocks;
};

} // namespace

namespace {

const QChar kOpeners[] = { QLatin1Char('('), QLatin1Char('['), QLatin1Char('{') };
const QChar kClosers[] = { QLatin1Char(')'), QLatin1Char(']'), QLatin1Char('}') };

int BracketIndex(QChar c, const QChar (&set)[3])
{
    for (int i = 0; i < 3; ++i) {
        if (set[i] == c) return i;
    }
    return -1;
}

QString LeadingWhitespace(const QString& line)
{
    qsizetype n = 0;
    while (n < line.size() && (line[n] == QLatin1Char(' ') || line[n] == QLatin1Char('\t'))) ++n;
    return line.left(n);
}

// Per-line marks live on the block, so they move with the line as text is edited.
class BlockMarks final : public QTextBlockUserData
{
public:
    QString error;
    bool breakpoint = false;
};

const BlockMarks* MarksOf(const QTextBlock& block)
{
    return static_cast<const BlockMarks*>(block.userData());
}

BlockMarks* EditMarks(QTextBlock block)
{
    if (!block.userData()) block.setUserData(new BlockMarks);
    return static_cast<BlockMarks*>(block.userData());
}

bool HasError(const QTextBlock& block)
{
    return MarksOf(block) && !MarksOf(block)->error.isEmpty();
}

bool HasBreakpoint(const QTextBlock& block)
{
    return MarksOf(block) && MarksOf(block)->breakpoint;
}

QIcon CompletionIcon(uint32_t kind)
{
    switch (kind) {
    case HP_COMPLETION_KEYWORD: return Theme::Icon(QStringLiteral("code"), Theme::IconTone::Muted);
    case HP_COMPLETION_FUNCTION: return Theme::Icon(QStringLiteral("logic"), Theme::IconTone::Action);
    case HP_COMPLETION_FIELD: return Theme::Icon(QStringLiteral("list"), Theme::IconTone::Info);
    case HP_COMPLETION_TYPE: return Theme::Icon(QStringLiteral("cube"), Theme::IconTone::Warning);
    default: return Theme::Icon(QStringLiteral("dot"), Theme::IconTone::Secondary);
    }
}

constexpr int kCompletionDetailRole = Qt::UserRole + 1;

class CompletionDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        const QString detail = index.data(kCompletionDetailRole).toString();
        if (detail.isEmpty()) return;
        painter->save();
        painter->setPen(Theme::Color(Theme::Token::TextMuted));
        painter->drawText(option.rect.adjusted(0, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter, detail);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 22));
        return size;
    }
};

} // namespace

class CodeEditor final : public QPlainTextEdit
{
public:
    CodeEditor(const QString& path, bool crlf, QWidget* parent)
        : QPlainTextEdit(parent)
        , m_path(path)
        , m_crlf(crlf)
        , m_gutter(new Gutter(this))
        , m_minimap(new Minimap(this))
    {
        setObjectName(QStringLiteral("CodeEditor"));
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setFrameShape(QFrame::NoFrame);
        QFont font;
        font.setFamilies({ QStringLiteral("JetBrains Mono"), QStringLiteral("Consolas") });
        font.setStyleHint(QFont::Monospace);
        font.setPixelSize(13);
        setFont(font);
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);

        connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() { updateMargins(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
            if (dy) m_gutter->scroll(0, dy);
            else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
            m_minimap->update();
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
            updateExtraSelections();
            m_gutter->update();
        });

        m_completionModel = new QStandardItemModel(this);
        m_completer = new QCompleter(m_completionModel, this);
        m_completer->setWidget(this);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setModelSorting(QCompleter::UnsortedModel);
        m_completer->setMaxVisibleItems(10);
        m_completer->popup()->setObjectName(QStringLiteral("CodeCompletionPopup"));
        m_completer->popup()->setItemDelegate(new CompletionDelegate(m_completer->popup()));
        connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this,
                [this](const QString& text) { insertCompletion(text); });

        updateMargins();
        updateExtraSelections();
    }

    const QString& path() const { return m_path; }
    bool crlf() const { return m_crlf; }
    QByteArray diskBytes;
    bool changedOnDisk = false;
    QString lineComment;

    std::function<void()> onSave;
    std::function<void()> onClose;
    std::function<void(bool replace)> onFind;
    std::function<void()> onFindNext;
    std::function<void()> onGoToLine;

    void detectIndentation()
    {
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            const QString indent = LeadingWhitespace(block.text());
            if (indent.isEmpty()) continue;
            m_indentUnit = indent.startsWith(QLatin1Char('\t')) ? QStringLiteral("\t") : QStringLiteral("    ");
            return;
        }
    }

    void setSearchMatches(QList<QTextCursor> matches)
    {
        m_searchMatches = std::move(matches);
        updateExtraSelections();
    }

    void goToLine(int line, int column = 1)
    {
        const QTextBlock block = document()->findBlockByNumber(qBound(0, line - 1, blockCount() - 1));
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, qBound(0, column - 1, block.length() - 1));
        setTextCursor(cursor);
        centerCursor();
        setFocus();
    }

    void markError(int line, const QString& message)
    {
        const QTextBlock block = document()->findBlockByNumber(line - 1);
        if (!block.isValid()) return;
        EditMarks(block)->error = message;
        refreshMarks();
    }

    void clearErrors()
    {
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (MarksOf(block)) EditMarks(block)->error.clear();
        }
        refreshMarks();
    }

    QList<int> breakpoints() const
    {
        QList<int> lines;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (HasBreakpoint(block)) lines << block.blockNumber() + 1;
        }
        return lines;
    }

    void setBreakpoints(const QList<int>& lines)
    {
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (MarksOf(block)) EditMarks(block)->breakpoint = false;
        }
        for (int line : lines) {
            const QTextBlock block = document()->findBlockByNumber(line - 1);
            if (block.isValid()) EditMarks(block)->breakpoint = true;
        }
        refreshMarks();
    }

    /// 1-based line where the debugger stopped, or 0.
    void setExecutionLine(int line)
    {
        m_executionLine = line;
        refreshMarks();
    }

    std::function<void()> onBreakpointsChanged;

protected:
    // Claims editor shortcuts before the main window's menus and panels see the same keys.
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->matches(QKeySequence::Save) || key->matches(QKeySequence::Close)
                || key->matches(QKeySequence::Find) || key->matches(QKeySequence::Replace)
                || key->matches(QKeySequence::FindNext) || isGoToLine(key) || isToggleComment(key)
                || isCompletionShortcut(key)) {
                event->accept();
                return true;
            }
        }
        return QPlainTextEdit::event(event);
    }

    bool viewportEvent(QEvent* event) override
    {
        if (event->type() == QEvent::Resize) {
            const QRect view = viewport()->geometry();
            m_minimap->setGeometry(view.right() + 1, view.top(), kMinimapWidth, view.height());
        } else if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const QTextCursor at = cursorForPosition(help->pos());
            QString text = HasError(at.block()) ? MarksOf(at.block())->error : QString();
            if (text.isEmpty()) {
                const QString line = at.block().text();
                const uint32_t column = static_cast<uint32_t>(line.left(at.positionInBlock()).toUtf8().size()) + 1;
                text = QString::fromStdString(ScriptLanguageRegistry::Instance().Hover(
                    m_path.toStdString(), toPlainText().toStdString(),
                    static_cast<uint32_t>(at.blockNumber() + 1), column));
            }
            if (text.isEmpty()) QToolTip::hideText();
            else QToolTip::showText(help->globalPos(), text, this);
            return true;
        }
        return QPlainTextEdit::viewportEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect area = contentsRect();
        m_gutter->setGeometry(area.left(), area.top(), gutterWidth(), area.height());
        m_minimap->setVisible(width() >= kMinimapMinEditorWidth);
        updateMargins();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        // The completer's popup handles choosing and dismissing.
        if (m_completer->popup()->isVisible()) {
            switch (event->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
            case Qt::Key_Escape:
                event->ignore();
                return;
            default:
                break;
            }
        }
        if (isCompletionShortcut(event)) return showCompletions(true);
        keyPressEditing(event);
        const QString typed = event->text();
        const bool wordChar = typed.size() == 1 && (typed[0].isLetterOrNumber() || typed[0] == QLatin1Char('_'));
        const bool member = typed == QStringLiteral(".") || typed == QStringLiteral(":");
        if (wordChar || member || (event->key() == Qt::Key_Backspace && m_completer->popup()->isVisible())) {
            showCompletions(false);
        } else if (m_completer->popup()->isVisible()) {
            m_completer->popup()->hide();
        }
    }

    void keyPressEditing(QKeyEvent* event)
    {
        if (event->matches(QKeySequence::Save)) return call(onSave);
        if (event->matches(QKeySequence::Close)) return call(onClose);
        if (event->matches(QKeySequence::Find)) return onFind ? onFind(false) : void();
        if (event->matches(QKeySequence::Replace)) return onFind ? onFind(true) : void();
        if (event->matches(QKeySequence::FindNext)) return call(onFindNext);
        if (isGoToLine(event)) return call(onGoToLine);
        if (isToggleComment(event)) return toggleComment();

        const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
        if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && (mods & Qt::AltModifier)) {
            const bool down = event->key() == Qt::Key_Down;
            return (mods & Qt::ShiftModifier) ? duplicateLines(down) : moveLines(down);
        }
        if (event->key() == Qt::Key_Tab && mods == Qt::NoModifier) {
            if (spansLines()) return shiftLines(true);
            return textCursor().insertText(m_indentUnit);
        }
        if (event->key() == Qt::Key_Backtab) return shiftLines(false);
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && mods == Qt::NoModifier) {
            return newLine();
        }
        if (event->key() == Qt::Key_Backspace && mods == Qt::NoModifier && deletePair()) return;
        if (mods == Qt::NoModifier || mods == Qt::ShiftModifier) {
            const QString text = event->text();
            if (text.size() == 1 && typeBracketOrQuote(text[0])) return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

private:
    class Gutter final : public QWidget
    {
    public:
        explicit Gutter(CodeEditor* editor) : QWidget(editor), m_editor(editor)
        {
            setObjectName(QStringLiteral("CodeGutter"));
        }
        QSize sizeHint() const override { return { m_editor->gutterWidth(), 0 }; }

    protected:
        void paintEvent(QPaintEvent* event) override { m_editor->paintGutter(event->rect()); }
        void mousePressEvent(QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton) m_editor->toggleBreakpointAt(event->position().y());
        }

    private:
        CodeEditor* m_editor;
    };

    class Minimap final : public QWidget
    {
    public:
        explicit Minimap(CodeEditor* editor) : QWidget(editor), m_editor(editor)
        {
            setCursor(Qt::PointingHandCursor);
        }

    protected:
        void paintEvent(QPaintEvent*) override { m_editor->paintMinimap(); }
        void mousePressEvent(QMouseEvent* event) override { m_editor->scrollFromMinimap(event->position().y()); }
        void mouseMoveEvent(QMouseEvent* event) override
        {
            if (event->buttons() & Qt::LeftButton) m_editor->scrollFromMinimap(event->position().y());
        }

    private:
        CodeEditor* m_editor;
    };

    static constexpr int kMinimapWidth = 110;
    static constexpr int kMinimapMinEditorWidth = 640;
    static constexpr int kMinimapLineHeight = 2;
    static constexpr int kMinimapTabColumns = 4;

    static void call(const std::function<void()>& callback)
    {
        if (callback) callback();
    }

    static bool isGoToLine(const QKeyEvent* key)
    {
        return key->key() == Qt::Key_G && (key->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
    }

    static bool isToggleComment(const QKeyEvent* key)
    {
        return key->key() == Qt::Key_Slash && (key->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
    }

    static bool isCompletionShortcut(const QKeyEvent* key)
    {
        return key->key() == Qt::Key_Space && (key->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
    }

    void refreshMarks()
    {
        updateExtraSelections();
        m_gutter->update();
        m_minimap->update();
    }

    void toggleBreakpointAt(qreal y)
    {
        for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
            const QRectF bounds = blockBoundingGeometry(block).translated(contentOffset());
            if (y < bounds.top()) return;
            if (y <= bounds.bottom()) {
                BlockMarks* marks = EditMarks(block);
                marks->breakpoint = !marks->breakpoint;
                refreshMarks();
                call(onBreakpointsChanged);
                return;
            }
        }
    }

    // Offers the language's suggestions for the word before the cursor; forced also opens with no prefix.
    void showCompletions(bool forced)
    {
        const QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        const int column = cursor.positionInBlock();
        int start = column;
        while (start > 0 && (line[start - 1].isLetterOrNumber() || line[start - 1] == QLatin1Char('_'))) --start;
        const QString prefix = line.mid(start, column - start);
        const bool member = start > 0 && (line[start - 1] == QLatin1Char('.') || line[start - 1] == QLatin1Char(':'));
        if ((!forced && !member && prefix.size() < 2) || !isCode(cursor.block(), qMax(0, column - 1))) {
            m_completer->popup()->hide();
            return;
        }

        const auto completions = ScriptLanguageRegistry::Instance().Complete(
            m_path.toStdString(), toPlainText().toStdString(), static_cast<uint32_t>(cursor.blockNumber() + 1),
            static_cast<uint32_t>(line.left(column).toUtf8().size()) + 1);
        m_completionModel->clear();
        QSet<QString> seen;
        for (const auto& completion : completions) {
            const QString label = QString::fromStdString(completion.label);
            if (seen.contains(label)) continue;
            seen.insert(label);
            auto* item = new QStandardItem(CompletionIcon(completion.kind), label);
            item->setData(QString::fromStdString(completion.detail), kCompletionDetailRole);
            m_completionModel->appendRow(item);
        }
        // Languages without a provider still get the file's own words.
        if (completions.empty() && !member) {
            static const QRegularExpression word(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{2,}"));
            const QString text = toPlainText();
            for (auto it = word.globalMatch(text); it.hasNext();) {
                const QString label = it.next().captured();
                if (label == prefix || seen.contains(label)) continue;
                seen.insert(label);
                m_completionModel->appendRow(new QStandardItem(CompletionIcon(HP_COMPLETION_VARIABLE), label));
            }
        }

        m_completer->setCompletionPrefix(prefix);
        const bool onlyExact = m_completer->completionCount() == 1
            && m_completer->currentCompletion().compare(prefix, Qt::CaseSensitive) == 0;
        if (m_completer->completionCount() == 0 || onlyExact) {
            m_completer->popup()->hide();
            return;
        }
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
        QRect rect = cursorRect();
        rect.translate(viewportMargins().left(), 0);
        rect.setWidth(qMax(280, m_completer->popup()->sizeHintForColumn(0) + 120));
        m_completer->complete(rect);
    }

    void insertCompletion(const QString& text)
    {
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, int(m_completer->completionPrefix().size()));
        cursor.insertText(text);
        setTextCursor(cursor);
    }

    bool spansLines() const
    {
        const QTextCursor cursor = textCursor();
        return cursor.hasSelection()
            && document()->findBlock(cursor.selectionStart()) != document()->findBlock(cursor.selectionEnd());
    }

    // The blocks the selection touches; a selection ending at a line start leaves that line out.
    QPair<QTextBlock, QTextBlock> selectedBlocks() const
    {
        const QTextCursor cursor = textCursor();
        const QTextBlock first = document()->findBlock(cursor.selectionStart());
        QTextBlock last = document()->findBlock(cursor.selectionEnd());
        if (cursor.hasSelection() && last != first && cursor.selectionEnd() == last.position()) {
            last = last.previous();
        }
        return { first, last };
    }

    void selectLines(int firstNumber, int lastNumber)
    {
        const QTextBlock first = document()->findBlockByNumber(firstNumber);
        const QTextBlock last = document()->findBlockByNumber(lastNumber);
        QTextCursor cursor(first);
        cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
    }

    void shiftLines(bool indent)
    {
        const auto [first, last] = selectedBlocks();
        const int firstNumber = first.blockNumber();
        const int lastNumber = last.blockNumber();
        const bool hadSelection = textCursor().hasSelection();
        QTextCursor edit(document());
        edit.beginEditBlock();
        for (int number = firstNumber; number <= lastNumber; ++number) {
            const QTextBlock block = document()->findBlockByNumber(number);
            QTextCursor line(block);
            if (indent) {
                if (!block.text().isEmpty()) line.insertText(m_indentUnit);
                continue;
            }
            const QString text = block.text();
            int remove = 0;
            if (text.startsWith(QLatin1Char('\t'))) {
                remove = 1;
            } else {
                while (remove < 4 && remove < text.size() && text[remove] == QLatin1Char(' ')) ++remove;
            }
            line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
            line.removeSelectedText();
        }
        edit.endEditBlock();
        if (hadSelection) selectLines(firstNumber, lastNumber);
    }

    void toggleComment()
    {
        if (lineComment.isEmpty()) return;
        const auto [first, last] = selectedBlocks();
        const int firstNumber = first.blockNumber();
        const int lastNumber = last.blockNumber();
        bool allCommented = true;
        int indent = INT_MAX;
        for (int number = firstNumber; number <= lastNumber; ++number) {
            const QString text = document()->findBlockByNumber(number).text();
            if (text.trimmed().isEmpty()) continue;
            allCommented &= text.trimmed().startsWith(lineComment);
            indent = qMin(indent, int(LeadingWhitespace(text).size()));
        }
        if (indent == INT_MAX) return;
        QTextCursor edit(document());
        edit.beginEditBlock();
        for (int number = firstNumber; number <= lastNumber; ++number) {
            const QTextBlock block = document()->findBlockByNumber(number);
            const QString text = block.text();
            if (text.trimmed().isEmpty()) continue;
            QTextCursor line(block);
            if (allCommented) {
                const int at = int(text.indexOf(lineComment));
                const int length = int(lineComment.size()) + (text.mid(at + lineComment.size(), 1) == QStringLiteral(" "));
                line.setPosition(block.position() + at);
                line.setPosition(block.position() + at + length, QTextCursor::KeepAnchor);
                line.removeSelectedText();
            } else {
                line.setPosition(block.position() + indent);
                line.insertText(lineComment + QLatin1Char(' '));
            }
        }
        edit.endEditBlock();
    }

    void moveLines(bool down)
    {
        const auto [first, last] = selectedBlocks();
        const QTextBlock neighbour = down ? last.next() : first.previous();
        if (!neighbour.isValid()) return;
        const QTextCursor before = textCursor();
        const int anchorOffset = before.anchor() - first.position();
        const int positionOffset = before.position() - first.position();
        const int firstNumber = first.blockNumber();
        QStringList lines;
        for (QTextBlock block = first;; block = block.next()) {
            lines << block.text();
            if (block == last) break;
        }
        const QString moved = lines.join(QLatin1Char('\n'));

        QTextCursor edit(document());
        edit.beginEditBlock();
        edit.setPosition(down ? first.position() : neighbour.position());
        const QTextBlock end = down ? neighbour : last;
        edit.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
        edit.insertText(down ? neighbour.text() + QLatin1Char('\n') + moved : moved + QLatin1Char('\n') + neighbour.text());
        edit.endEditBlock();

        const int start = document()->findBlockByNumber(firstNumber + (down ? 1 : -1)).position();
        QTextCursor after(document());
        after.setPosition(start + anchorOffset);
        after.setPosition(start + positionOffset, QTextCursor::KeepAnchor);
        setTextCursor(after);
    }

    void duplicateLines(bool down)
    {
        const auto [first, last] = selectedBlocks();
        QTextCursor cursor = textCursor();
        const int anchor = cursor.anchor();
        const int position = cursor.position();
        QTextCursor copy(first);
        copy.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        const QString text = copy.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        QTextCursor edit(document());
        edit.beginEditBlock();
        edit.setPosition(last.position() + last.length() - 1);
        edit.insertText(QLatin1Char('\n') + text);
        edit.endEditBlock();
        const int shift = down ? int(text.size()) + 1 : 0;
        cursor.setPosition(anchor + shift);
        cursor.setPosition(position + shift, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
    }

    void newLine()
    {
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        const QString indent = LeadingWhitespace(line);
        const int column = cursor.positionInBlock();
        const QChar before = column > 0 ? line[column - 1] : QChar();
        const QChar after = column < line.size() ? line[column] : QChar();
        const int open = BracketIndex(before, kOpeners);
        cursor.beginEditBlock();
        if (open >= 0 && after == kClosers[open]) {
            cursor.insertText(QLatin1Char('\n') + indent + m_indentUnit);
            const int caret = cursor.position();
            cursor.insertText(QLatin1Char('\n') + indent);
            cursor.setPosition(caret);
        } else {
            cursor.insertText(QLatin1Char('\n') + indent + (open >= 0 ? m_indentUnit : QString()));
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        ensureCursorVisible();
    }

    bool typeBracketOrQuote(QChar typed)
    {
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        const int column = cursor.positionInBlock();
        const QChar after = column < line.size() ? line[column] : QChar();
        const bool isQuote = typed == QLatin1Char('"') || typed == QLatin1Char('\'');
        if (!cursor.hasSelection() && after == typed && (isQuote || BracketIndex(typed, kClosers) >= 0)) {
            cursor.movePosition(QTextCursor::Right);
            setTextCursor(cursor);
            return true;
        }
        const int open = BracketIndex(typed, kOpeners);
        if (open < 0 && !isQuote) return false;
        const QChar close = isQuote ? typed : kClosers[open];
        if (cursor.hasSelection()) {
            if (spansLines()) return false;
            cursor.insertText(typed + cursor.selectedText() + close);
            return true;
        }
        const QChar before = column > 0 ? line[column - 1] : QChar();
        const bool afterAllowsPair = after.isNull() || after.isSpace() || BracketIndex(after, kClosers) >= 0
            || after == QLatin1Char(',') || after == QLatin1Char(';');
        if (!afterAllowsPair || (isQuote && (before.isLetterOrNumber() || before == typed))) return false;
        cursor.insertText(QString(typed) + close);
        cursor.movePosition(QTextCursor::Left);
        setTextCursor(cursor);
        return true;
    }

    bool deletePair()
    {
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) return false;
        const QString line = cursor.block().text();
        const int column = cursor.positionInBlock();
        if (column == 0 || column >= line.size()) return false;
        const QChar before = line[column - 1];
        const QChar after = line[column];
        const int open = BracketIndex(before, kOpeners);
        const bool pair = (open >= 0 && after == kClosers[open])
            || ((before == QLatin1Char('"') || before == QLatin1Char('\'')) && after == before);
        if (!pair) return false;
        cursor.movePosition(QTextCursor::Left);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
        cursor.removeSelectedText();
        return true;
    }

    // Strings and comments are skipped so a bracket inside "(" does not pair with code.
    bool isCode(const QTextBlock& block, int column) const
    {
        const QColor comment = Theme::Color(Theme::Token::CodeComment);
        const QColor string = Theme::Color(Theme::Token::CodeString);
        for (const QTextLayout::FormatRange& range : block.layout()->formats()) {
            if (column >= range.start && column < range.start + range.length) {
                const QColor color = range.format.foreground().color();
                if (color == comment || color == string) return false;
            }
        }
        return true;
    }

    int matchingBracket(int position) const
    {
        const QChar c = document()->characterAt(position);
        int index = BracketIndex(c, kOpeners);
        const bool forward = index >= 0;
        if (!forward) index = BracketIndex(c, kClosers);
        const QTextBlock startBlock = document()->findBlock(position);
        if (index < 0 || !isCode(startBlock, position - startBlock.position())) return -1;
        const QChar other = forward ? kClosers[index] : kOpeners[index];
        constexpr int kMaxScan = 20000;
        int depth = 0;
        for (int p = position, steps = 0; p >= 0 && p < document()->characterCount() && steps < kMaxScan;
             p += forward ? 1 : -1, ++steps) {
            const QChar ch = document()->characterAt(p);
            if (ch != c && ch != other) continue;
            const QTextBlock block = document()->findBlock(p);
            if (!isCode(block, p - block.position())) continue;
            depth += ch == c ? 1 : -1;
            if (depth == 0) return p;
        }
        return -1;
    }

    void updateExtraSelections()
    {
        QList<QTextEdit::ExtraSelection> selections;
        QTextEdit::ExtraSelection line;
        line.format.setBackground(Theme::Color(Theme::Token::CodeLineCurrent));
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        selections << line;

        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            const bool executing = block.blockNumber() + 1 == m_executionLine;
            if (!executing && !HasBreakpoint(block)) continue;
            QTextEdit::ExtraSelection marked;
            marked.format.setBackground(Theme::Color(executing ? Theme::Token::StatusWarningBg
                                                               : Theme::Token::CodeLineBreakpoint));
            marked.format.setProperty(QTextFormat::FullWidthSelection, true);
            marked.cursor = QTextCursor(block);
            selections << marked;
        }

        QColor matchColor = Theme::Color(Theme::Token::AccentSelection);
        matchColor.setAlpha(70);
        for (const QTextCursor& match : m_searchMatches) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(matchColor);
            selection.cursor = match;
            selections << selection;
        }

        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (!HasError(block) || block.text().trimmed().isEmpty()) continue;
            QTextEdit::ExtraSelection error;
            error.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            error.format.setUnderlineColor(Theme::Color(Theme::Token::StatusError));
            error.cursor = QTextCursor(document());
            error.cursor.setPosition(block.position() + int(LeadingWhitespace(block.text()).size()));
            error.cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            selections << error;
        }

        const int position = textCursor().position();
        for (int at : { position, position - 1 }) {
            const int match = at >= 0 ? matchingBracket(at) : -1;
            if (match < 0) continue;
            for (int p : { at, match }) {
                QTextEdit::ExtraSelection bracket;
                bracket.format.setBackground(Theme::Color(Theme::Token::SurfaceSelected));
                bracket.format.setForeground(Theme::Color(Theme::Token::TextPrimary));
                bracket.cursor = QTextCursor(document());
                bracket.cursor.setPosition(p);
                bracket.cursor.setPosition(p + 1, QTextCursor::KeepAnchor);
                selections << bracket;
            }
            break;
        }
        setExtraSelections(selections);
    }

    int visibleLines() const { return qMax(1, viewport()->height() / qMax(1, fontMetrics().lineSpacing())); }

    // First line drawn at the minimap's top: the map scrolls with the file once it is taller than the strip.
    int minimapFirstLine() const
    {
        const int total = blockCount();
        const int fitting = m_minimap->height() / kMinimapLineHeight;
        if (total <= fitting) return 0;
        const int scrollRange = qMax(1, total - visibleLines());
        const double progress = qBound(0.0, double(verticalScrollBar()->value()) / scrollRange, 1.0);
        return qRound(progress * (total - fitting));
    }

    void paintMinimap()
    {
        QPainter painter(m_minimap);
        painter.fillRect(m_minimap->rect(), Theme::Color(Theme::Token::SurfaceBase));
        const int firstLine = minimapFirstLine();

        QColor viewColor = Theme::Color(Theme::Token::TextPrimary);
        viewColor.setAlpha(28);
        painter.fillRect(0, (verticalScrollBar()->value() - firstLine) * kMinimapLineHeight, m_minimap->width(),
                         visibleLines() * kMinimapLineHeight, viewColor);

        const int maxColumns = m_minimap->width() - 8;
        int y = 0;
        for (QTextBlock block = document()->findBlockByNumber(firstLine);
             block.isValid() && y < m_minimap->height(); block = block.next(), y += kMinimapLineHeight) {
            const QString text = block.text();
            const QList<QTextLayout::FormatRange> formats = block.layout()->formats();
            int column = 0;
            for (qsizetype i = 0; i < text.size() && column < maxColumns;) {
                if (text[i] == QLatin1Char('\t')) {
                    column += kMinimapTabColumns;
                    ++i;
                    continue;
                }
                if (text[i].isSpace()) {
                    ++column;
                    ++i;
                    continue;
                }
                QColor color = Theme::Color(Theme::Token::CodePlain);
                for (const QTextLayout::FormatRange& range : formats) {
                    if (i >= range.start && i < range.start + range.length && range.format.hasProperty(QTextFormat::ForegroundBrush)) {
                        color = range.format.foreground().color();
                    }
                }
                color.setAlpha(170);
                const int start = column;
                while (i < text.size() && !text[i].isSpace() && column < maxColumns) {
                    ++column;
                    ++i;
                }
                painter.fillRect(4 + start, y, column - start, kMinimapLineHeight - 1, color);
            }
            if (HasError(block) || HasBreakpoint(block)) {
                painter.fillRect(m_minimap->width() - 4, y, 4, kMinimapLineHeight, Theme::Color(Theme::Token::StatusError));
            }
        }
    }

    void scrollFromMinimap(qreal y)
    {
        const int line = minimapFirstLine() + int(y) / kMinimapLineHeight;
        verticalScrollBar()->setValue(line - visibleLines() / 2);
    }

    int gutterWidth() const
    {
        const int digits = QString::number(qMax(1, blockCount())).size();
        return fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits) + 28;
    }

    void updateMargins()
    {
        setViewportMargins(gutterWidth(), 0, m_minimap->isVisibleTo(this) ? kMinimapWidth : 0, 0);
    }

    void paintGutter(const QRect& rect)
    {
        QPainter painter(m_gutter);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect, Theme::Color(Theme::Token::SurfaceBase));
        painter.setFont(font());
        const int current = textCursor().blockNumber();
        QTextBlock block = firstVisibleBlock();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        while (block.isValid() && top <= rect.bottom()) {
            const int height = qRound(blockBoundingRect(block).height());
            if (block.isVisible() && top + height >= rect.top()) {
                const QPointF marker(10, top + height / 2.0);
                if (HasBreakpoint(block)) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(Theme::Color(Theme::Token::StatusError));
                    painter.drawEllipse(marker, 5, 5);
                } else if (HasError(block)) {
                    Theme::Icon(QStringLiteral("error"), Theme::IconTone::Error)
                        .paint(&painter, QRect(int(marker.x()) - 6, int(marker.y()) - 6, 12, 12));
                }
                if (block.blockNumber() + 1 == m_executionLine) {
                    Theme::Icon(QStringLiteral("play"), Theme::IconTone::Warning)
                        .paint(&painter, QRect(int(marker.x()) - 6, int(marker.y()) - 6, 12, 12));
                }
                painter.setPen(Theme::Color(block.blockNumber() == current ? Theme::Token::TextPrimary
                                                                           : Theme::Token::TextMuted));
                painter.drawText(0, top, m_gutter->width() - 12, height, Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(block.blockNumber() + 1));
            }
            block = block.next();
            top += height;
        }
    }

    QString m_path;
    bool m_crlf = false;
    QString m_indentUnit = QStringLiteral("    ");
    QList<QTextCursor> m_searchMatches;
    int m_executionLine = 0;
    Gutter* m_gutter = nullptr;
    Minimap* m_minimap = nullptr;
    QCompleter* m_completer = nullptr;
    QStandardItemModel* m_completionModel = nullptr;
};

namespace {

QToolButton* BarButton(const QString& icon, const QString& text, const QString& tip, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("CodeBarButton"));
    if (!icon.isEmpty()) button->setIcon(Theme::Icon(icon));
    if (!text.isEmpty()) button->setText(text);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QString LanguageName(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare(QStringLiteral("lua"), Qt::CaseInsensitive) == 0) return QStringLiteral("Lua");
    if (const HPScriptLanguage* language = ScriptLanguageRegistry::Instance().FindForPath(path.toStdString())) {
        return QString::fromUtf8(language->displayName);
    }
    return suffix.isEmpty() ? QObject::tr("Plain Text") : suffix.toUpper();
}

QRegularExpression SearchExpression(const QString& text, bool matchCase, bool wholeWord, bool regex)
{
    QString pattern = regex ? text : QRegularExpression::escape(text);
    if (wholeWord) pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
    return QRegularExpression(pattern, matchCase ? QRegularExpression::NoPatternOption
                                                 : QRegularExpression::CaseInsensitiveOption);
}

} // namespace

CodeEditorPanel::CodeEditorPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("CodeEditorPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabs = new QTabBar(this);
    m_tabs->setObjectName(QStringLiteral("CodeTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setExpanding(false);
    m_tabs->setElideMode(Qt::ElideMiddle);
    layout->addWidget(m_tabs);

    m_breadcrumbs = new QLabel(this);
    m_breadcrumbs->setObjectName(QStringLiteral("CodeBreadcrumbs"));
    m_breadcrumbs->setTextFormat(Qt::PlainText);
    layout->addWidget(m_breadcrumbs);

    m_banner = new QWidget(this);
    m_banner->setObjectName(QStringLiteral("CodeFileBanner"));
    m_banner->setAttribute(Qt::WA_StyledBackground, true);
    auto* bannerRow = new QHBoxLayout(m_banner);
    bannerRow->setContentsMargins(12, 6, 8, 6);
    auto* bannerIcon = new QLabel(m_banner);
    bannerIcon->setPixmap(Theme::Icon(QStringLiteral("warning"), Theme::IconTone::Warning)
                              .pixmap(QSize(16, 16), devicePixelRatioF()));
    bannerRow->addWidget(bannerIcon);
    m_bannerText = new QLabel(m_banner);
    bannerRow->addWidget(m_bannerText, 1);
    m_bannerReload = new QPushButton(tr("Reload"), m_banner);
    bannerRow->addWidget(m_bannerReload);
    auto* keep = new QPushButton(tr("Keep mine"), m_banner);
    bannerRow->addWidget(keep);
    connect(m_bannerReload, &QPushButton::clicked, this, [this]() {
        if (CodeEditor* editor = currentEditor()) reloadFromDisk(editor);
    });
    connect(keep, &QPushButton::clicked, this, [this]() {
        if (CodeEditor* editor = currentEditor()) editor->changedOnDisk = false;
        updateBanner();
    });
    m_banner->hide();
    layout->addWidget(m_banner);

    m_findBar = new QWidget(this);
    m_findBar->setObjectName(QStringLiteral("CodeFindBar"));
    m_findBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* findLayout = new QVBoxLayout(m_findBar);
    findLayout->setContentsMargins(8, 6, 8, 6);
    findLayout->setSpacing(4);
    auto* findRow = new QHBoxLayout;
    findRow->setSpacing(4);
    m_findField = new QLineEdit(m_findBar);
    m_findField->setPlaceholderText(tr("Find"));
    m_findField->addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
    findRow->addWidget(m_findField, 1);
    m_matchCase = BarButton({}, QStringLiteral("Aa"), tr("Match case"), m_findBar);
    m_wholeWord = BarButton({}, QStringLiteral("ab"), tr("Whole word"), m_findBar);
    m_useRegex = BarButton({}, QStringLiteral(".*"), tr("Regular expression"), m_findBar);
    for (QToolButton* toggle : { m_matchCase, m_wholeWord, m_useRegex }) {
        toggle->setCheckable(true);
        connect(toggle, &QToolButton::toggled, this, [this]() { runSearch(false); });
        findRow->addWidget(toggle);
    }
    m_findCount = new QLabel(m_findBar);
    m_findCount->setProperty("role", "muted");
    m_findCount->setMinimumWidth(72);
    m_findCount->setAlignment(Qt::AlignCenter);
    findRow->addWidget(m_findCount);
    auto* previous = BarButton({}, QStringLiteral("↑"), tr("Previous match (Shift+Enter)"), m_findBar);
    auto* next = BarButton({}, QStringLiteral("↓"), tr("Next match (Enter)"), m_findBar);
    auto* closeFind = BarButton(QStringLiteral("close"), {}, tr("Close (Esc)"), m_findBar);
    findRow->addWidget(previous);
    findRow->addWidget(next);
    findRow->addWidget(closeFind);
    findLayout->addLayout(findRow);

    m_replaceRow = new QWidget(m_findBar);
    auto* replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(4);
    m_replaceField = new QLineEdit(m_replaceRow);
    m_replaceField->setPlaceholderText(tr("Replace"));
    replaceLayout->addWidget(m_replaceField, 1);
    auto* replace = new QPushButton(tr("Replace"), m_replaceRow);
    auto* replaceEvery = new QPushButton(tr("Replace all"), m_replaceRow);
    replaceLayout->addWidget(replace);
    replaceLayout->addWidget(replaceEvery);
    findLayout->addWidget(m_replaceRow);
    m_findBar->hide();
    layout->addWidget(m_findBar);

    connect(m_findField, &QLineEdit::textChanged, this, [this]() { runSearch(true); });
    connect(previous, &QToolButton::clicked, this, [this]() { findNext(true); });
    connect(next, &QToolButton::clicked, this, [this]() { findNext(false); });
    connect(closeFind, &QToolButton::clicked, this, &CodeEditorPanel::hideFind);
    connect(replace, &QPushButton::clicked, this, &CodeEditorPanel::replaceOne);
    connect(replaceEvery, &QPushButton::clicked, this, &CodeEditorPanel::replaceAll);
    m_findField->installEventFilter(this);
    m_replaceField->installEventFilter(this);

    m_goToBar = new QWidget(this);
    m_goToBar->setObjectName(QStringLiteral("CodeFindBar"));
    m_goToBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* goToRow = new QHBoxLayout(m_goToBar);
    goToRow->setContentsMargins(8, 6, 8, 6);
    goToRow->setSpacing(6);
    auto* goToLabel = new QLabel(tr("Go to line"), m_goToBar);
    goToLabel->setProperty("role", "muted");
    goToRow->addWidget(goToLabel);
    m_goToField = new QLineEdit(m_goToBar);
    m_goToField->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d*(:\\d*)?")), m_goToField));
    goToRow->addWidget(m_goToField, 1);
    auto* closeGoTo = BarButton(QStringLiteral("close"), {}, tr("Close (Esc)"), m_goToBar);
    goToRow->addWidget(closeGoTo);
    connect(closeGoTo, &QToolButton::clicked, this, [this]() {
        m_goToBar->hide();
        if (CodeEditor* editor = currentEditor()) editor->setFocus();
    });
    connect(m_goToField, &QLineEdit::returnPressed, this, [this]() {
        CodeEditor* editor = currentEditor();
        const QStringList parts = m_goToField->text().split(QLatin1Char(':'));
        if (!editor || parts.value(0).isEmpty()) return;
        m_goToBar->hide();
        editor->goToLine(parts.value(0).toInt(), qMax(1, parts.value(1).toInt()));
    });
    m_goToField->installEventFilter(this);
    m_goToBar->hide();
    layout->addWidget(m_goToBar);

    m_stack = new QStackedWidget(this);
    m_emptyState = new QLabel(tr("Double-click a script in the Content Browser to edit it here."), m_stack);
    m_emptyState->setObjectName(QStringLiteral("CodeEmptyState"));
    static_cast<QLabel*>(m_emptyState)->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_emptyState);
    layout->addWidget(m_stack, 1);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &CodeEditorPanel::onFileChanged);

    connect(m_tabs, &QTabBar::currentChanged, this, [this]() { showCurrentTab(); });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int index) { closeTab(index); });

    showCurrentTab();
}

bool CodeEditorPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);
    const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    if (key->key() == Qt::Key_Escape) {
        if (watched == m_goToField) {
            m_goToBar->hide();
            if (CodeEditor* editor = currentEditor()) editor->setFocus();
        } else {
            hideFind();
        }
        return true;
    }
    if (enter && watched == m_findField) {
        findNext(key->modifiers() & Qt::ShiftModifier);
        return true;
    }
    if (enter && watched == m_replaceField) {
        replaceOne();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool CodeEditorPanel::OpenFile(const QString& absolutePath)
{
    const QFileInfo info(absolutePath);
    const QString path = info.canonicalFilePath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editorAt(i)->path() == path) {
            m_tabs->setCurrentIndex(i);
            editorAt(i)->setFocus();
            return true;
        }
    }

    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Cannot Open File"),
                             tr("Failed to open %1:\n%2").arg(absolutePath, file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    auto* editor = new CodeEditor(path, bytes.contains("\r\n"), m_stack);
    editor->setPlainText(QString::fromUtf8(bytes));
    editor->diskBytes = bytes;
    editor->detectIndentation();
    if (const HPSyntax* syntax = ScriptLanguageRegistry::Instance().FindSyntaxForPath(path.toStdString())) {
        new SyntaxHighlighter(*syntax, path, editor->document());
        editor->lineComment = QString::fromUtf8(syntax->lineComment ? syntax->lineComment : "");
    }
    for (const auto& [line, message] : m_pendingErrors.value(path)) editor->markError(line, message);
    editor->document()->setModified(false);
    editor->onSave = [this, editor]() { save(editor); };
    editor->onClose = [this, editor]() {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (editorAt(i) == editor) {
                closeTab(i);
                return;
            }
        }
    };
    editor->onFind = [this](bool replace) { showFind(replace); };
    editor->onFindNext = [this]() { findNext(false); };
    editor->onGoToLine = [this]() { showGoToLine(); };
    editor->setBreakpoints(m_breakpoints.value(path));
    editor->onBreakpointsChanged = [this, editor]() { syncBreakpoints(editor); };
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor]() { updateTab(editor); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        if (editor == currentEditor()) updateStatus();
    });
    m_stack->addWidget(editor);
    m_watcher->addPath(path);

    const QIcon fileIcon = Theme::FileIcon(info.fileName());
    const int index = m_tabs->addTab(fileIcon.isNull() ? Theme::Icon(QStringLiteral("code")) : fileIcon, info.fileName());
    m_tabs->setTabData(index, QVariant::fromValue<QObject*>(editor));
    m_tabs->setTabToolTip(index, path);
    // The first tab becomes current inside addTab, before its data is set.
    m_tabs->setCurrentIndex(index);
    showCurrentTab();
    editor->setFocus();
    return true;
}

bool CodeEditorPanel::OpenFileAt(const QString& absolutePath, int line)
{
    if (!OpenFile(absolutePath)) return false;
    if (CodeEditor* editor = currentEditor()) editor->goToLine(line);
    return true;
}

bool CodeEditorPanel::CloseAll()
{
    SaveSession();
    while (m_tabs->count() > 0) {
        if (!closeTab(m_tabs->count() - 1)) return false;
    }
    return true;
}

void CodeEditorPanel::MarkError(const QString& absolutePath, int line, const QString& message)
{
    const QString path = QFileInfo(absolutePath).canonicalFilePath();
    if (path.isEmpty() || line < 1) return;
    QList<QPair<int, QString>>& pending = m_pendingErrors[path];
    for (const auto& existing : pending) {
        if (existing.first == line && existing.second == message) return;
    }
    pending.append({ line, message });
    if (CodeEditor* editor = editorForPath(path)) editor->markError(line, message);
}

QString CodeEditorPanel::ResolveLocation(const QString& location)
{
    static QHash<QString, QString> cache;
    const QString root = QString::fromStdString(AssetManager::GetInstance().GetProjectRoot());
    const QString key = root + QLatin1Char('|') + location;
    if (const auto it = cache.constFind(key); it != cache.constEnd() && QFileInfo::exists(it.value())) {
        return it.value();
    }
    QString tail = QDir::fromNativeSeparators(location);
    if (tail.startsWith(QStringLiteral("..."))) tail = tail.mid(3);
    QString resolved;
    if (QFileInfo(tail).isAbsolute()) {
        if (QFileInfo::exists(tail)) resolved = QFileInfo(tail).canonicalFilePath();
    } else if (!root.isEmpty()) {
        const QString direct = QDir(root).filePath(tail);
        if (QFileInfo::exists(direct)) {
            resolved = QFileInfo(direct).canonicalFilePath();
        } else {
            QDirIterator it(root, { QFileInfo(tail).fileName() }, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString candidate = QDir::fromNativeSeparators(it.next());
                if (candidate.endsWith(tail, Qt::CaseInsensitive)) {
                    resolved = QFileInfo(candidate).canonicalFilePath();
                    break;
                }
            }
        }
    }
    if (!resolved.isEmpty()) cache.insert(key, resolved);
    return resolved;
}

CodeEditor* CodeEditorPanel::editorAt(int index) const
{
    return index < 0 ? nullptr : static_cast<CodeEditor*>(m_tabs->tabData(index).value<QObject*>());
}

CodeEditor* CodeEditorPanel::currentEditor() const
{
    return editorAt(m_tabs->currentIndex());
}

CodeEditor* CodeEditorPanel::editorForPath(const QString& path) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editorAt(i)->path() == path) return editorAt(i);
    }
    return nullptr;
}

bool CodeEditorPanel::save(CodeEditor* editor)
{
    QString text = editor->toPlainText();
    if (editor->crlf()) text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    const QByteArray bytes = text.toUtf8();
    QSaveFile file(editor->path());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Cannot Save File"),
                             tr("Failed to save %1:\n%2").arg(editor->path(), file.errorString()));
        return false;
    }
    editor->diskBytes = bytes;
    editor->changedOnDisk = false;
    // QSaveFile replaces the file, which can drop it from the watcher.
    if (!m_watcher->files().contains(editor->path())) m_watcher->addPath(editor->path());
    m_pendingErrors.remove(editor->path());
    syncBreakpoints(editor);
    editor->clearErrors();
    editor->document()->setModified(false);
    updateBanner();
    return true;
}

bool CodeEditorPanel::closeTab(int index)
{
    CodeEditor* editor = editorAt(index);
    if (!editor) return false;
    if (editor->document()->isModified()) {
        m_tabs->setCurrentIndex(index);
        const auto choice = QMessageBox::question(
            this, tr("Unsaved Changes"),
            tr("Save changes to %1 before closing?").arg(QFileInfo(editor->path()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !save(editor))) return false;
    }
    m_watcher->removePath(editor->path());
    m_tabs->removeTab(index);
    editor->deleteLater();
    return true;
}

void CodeEditorPanel::updateTab(CodeEditor* editor)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editorAt(i) == editor) {
            const QString name = QFileInfo(editor->path()).fileName();
            m_tabs->setTabText(i, editor->document()->isModified() ? name + QStringLiteral("  ●") : name);
        }
    }
}

void CodeEditorPanel::showCurrentTab()
{
    CodeEditor* editor = currentEditor();
    m_stack->setCurrentWidget(editor ? static_cast<QWidget*>(editor) : m_emptyState);
    m_breadcrumbs->setVisible(editor != nullptr);
    updateBanner();
    updateStatus();
    if (!editor) {
        m_findBar->hide();
        m_goToBar->hide();
        return;
    }
    if (m_findBar->isVisible()) runSearch(false);
    const QString root = QString::fromStdString(AssetManager::GetInstance().GetProjectRoot());
    QString relative = root.isEmpty() ? QString() : QDir(root).relativeFilePath(editor->path());
    if (relative.isEmpty() || relative.startsWith(QStringLiteral(".."))) relative = editor->path();
    m_breadcrumbs->setText(relative.split(QLatin1Char('/'), Qt::SkipEmptyParts).join(QStringLiteral("  ›  ")));
}

void CodeEditorPanel::updateStatus()
{
    if (!onStatusChanged) return;
    const CodeEditor* editor = currentEditor();
    if (!editor) {
        onStatusChanged({});
        return;
    }
    const QTextCursor cursor = editor->textCursor();
    QString status = tr("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
    if (cursor.hasSelection()) status += tr(" (%1 selected)").arg(cursor.selectionEnd() - cursor.selectionStart());
    const QString separator = QStringLiteral("  ·  ");
    onStatusChanged(status + separator + QStringLiteral("UTF-8") + separator
                    + (editor->crlf() ? QStringLiteral("CRLF") : QStringLiteral("LF")) + separator
                    + LanguageName(editor->path()));
}

void CodeEditorPanel::showFind(bool replace)
{
    CodeEditor* editor = currentEditor();
    if (!editor) return;
    m_goToBar->hide();
    m_replaceRow->setVisible(replace);
    m_findBar->show();
    const QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection() && !cursor.selectedText().contains(QChar::ParagraphSeparator)) {
        QSignalBlocker block(m_findField);
        m_findField->setText(cursor.selectedText());
    }
    QLineEdit* focus = replace && !m_findField->text().isEmpty() ? m_replaceField : m_findField;
    focus->setFocus();
    focus->selectAll();
    runSearch(false);
}

void CodeEditorPanel::hideFind()
{
    m_findBar->hide();
    if (CodeEditor* editor = currentEditor()) {
        editor->setSearchMatches({});
        editor->setFocus();
    }
}

void CodeEditorPanel::runSearch(bool moveCursor)
{
    CodeEditor* editor = currentEditor();
    if (!editor) return;
    const QString text = m_findField->text();
    const QRegularExpression expression =
        SearchExpression(text, m_matchCase->isChecked(), m_wholeWord->isChecked(), m_useRegex->isChecked());
    m_findField->setProperty("invalid", !text.isEmpty() && !expression.isValid());
    m_findField->style()->unpolish(m_findField);
    m_findField->style()->polish(m_findField);
    QList<QTextCursor> matches;
    if (!text.isEmpty() && expression.isValid()) {
        constexpr int kMaxMatches = 5000;
        QTextCursor match(editor->document());
        while (matches.size() < kMaxMatches) {
            match = editor->document()->find(expression, match);
            if (match.isNull()) break;
            if (!match.hasSelection()) {
                if (!match.movePosition(QTextCursor::NextCharacter)) break;
                continue;
            }
            matches << match;
        }
    }
    editor->setSearchMatches(matches);
    if (moveCursor && !matches.isEmpty()) {
        const int from = editor->textCursor().selectionStart();
        QTextCursor target = matches.first();
        for (const QTextCursor& match : matches) {
            if (match.selectionStart() >= from) {
                target = match;
                break;
            }
        }
        editor->setTextCursor(target);
        editor->centerCursor();
    }
    int current = 0;
    const QTextCursor cursor = editor->textCursor();
    for (int i = 0; i < matches.size(); ++i) {
        if (matches[i].selectionStart() == cursor.selectionStart() && matches[i].selectionEnd() == cursor.selectionEnd()) {
            current = i + 1;
            break;
        }
    }
    if (text.isEmpty()) m_findCount->clear();
    else if (matches.isEmpty()) m_findCount->setText(tr("No results"));
    else if (current) m_findCount->setText(tr("%1 of %2").arg(current).arg(matches.size()));
    else m_findCount->setText(tr("%1 found").arg(matches.size()));
}

void CodeEditorPanel::findNext(bool backward)
{
    CodeEditor* editor = currentEditor();
    if (!editor) return;
    if (!m_findBar->isVisible() || m_findField->text().isEmpty()) {
        showFind(false);
        return;
    }
    const QRegularExpression expression = SearchExpression(m_findField->text(), m_matchCase->isChecked(),
                                                           m_wholeWord->isChecked(), m_useRegex->isChecked());
    if (!expression.isValid()) return;
    const QTextDocument::FindFlags flags = backward ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    QTextCursor match = editor->document()->find(expression, editor->textCursor(), flags);
    if (match.isNull()) {
        QTextCursor wrap(editor->document());
        if (backward) wrap.movePosition(QTextCursor::End);
        match = editor->document()->find(expression, wrap, flags);
    }
    if (match.isNull() || !match.hasSelection()) return;
    editor->setTextCursor(match);
    editor->centerCursor();
    runSearch(false);
}

void CodeEditorPanel::replaceOne()
{
    CodeEditor* editor = currentEditor();
    if (!editor) return;
    const QRegularExpression expression = SearchExpression(m_findField->text(), m_matchCase->isChecked(),
                                                           m_wholeWord->isChecked(), m_useRegex->isChecked());
    if (m_findField->text().isEmpty() || !expression.isValid()) return;
    QTextCursor cursor = editor->textCursor();
    const QString selected = cursor.selectedText();
    const QRegularExpressionMatch match = expression.match(selected);
    if (cursor.hasSelection() && match.hasMatch() && match.capturedStart() == 0
        && match.capturedLength() == selected.size()) {
        cursor.insertText(m_useRegex->isChecked() ? QString(selected).replace(expression, m_replaceField->text())
                                                  : m_replaceField->text());
    }
    findNext(false);
}

void CodeEditorPanel::replaceAll()
{
    CodeEditor* editor = currentEditor();
    if (!editor) return;
    const QRegularExpression expression = SearchExpression(m_findField->text(), m_matchCase->isChecked(),
                                                           m_wholeWord->isChecked(), m_useRegex->isChecked());
    if (m_findField->text().isEmpty() || !expression.isValid()) return;
    QTextCursor edit(editor->document());
    edit.beginEditBlock();
    QTextCursor match(editor->document());
    int count = 0;
    for (;;) {
        match = editor->document()->find(expression, match);
        if (match.isNull()) break;
        if (!match.hasSelection()) {
            if (!match.movePosition(QTextCursor::NextCharacter)) break;
            continue;
        }
        const QString selected = match.selectedText();
        match.insertText(m_useRegex->isChecked() ? QString(selected).replace(expression, m_replaceField->text())
                                                 : m_replaceField->text());
        ++count;
    }
    edit.endEditBlock();
    runSearch(false);
    m_findCount->setText(tr("%1 replaced").arg(count));
}

void CodeEditorPanel::showGoToLine()
{
    CodeEditor* editor = currentEditor();
    if (!editor) return;
    m_findBar->hide();
    editor->setSearchMatches({});
    m_goToField->setPlaceholderText(tr("Line 1 to %1, or line:column").arg(editor->blockCount()));
    m_goToField->clear();
    m_goToBar->show();
    m_goToField->setFocus();
}

void CodeEditorPanel::onFileChanged(const QString& path)
{
    CodeEditor* editor = editorForPath(path);
    if (!editor) return;
    QFile file(path);
    if (!file.exists()) {
        editor->changedOnDisk = true;
        updateBanner();
        return;
    }
    if (!m_watcher->files().contains(path)) m_watcher->addPath(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = file.readAll();
    if (bytes == editor->diskBytes) return;
    if (editor->document()->isModified()) {
        editor->changedOnDisk = true;
        updateBanner();
        return;
    }
    reloadFromDisk(editor);
}

void CodeEditorPanel::reloadFromDisk(CodeEditor* editor)
{
    QFile file(editor->path());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = file.readAll();
    const int line = editor->textCursor().blockNumber();
    const int column = editor->textCursor().positionInBlock();
    const int scroll = editor->verticalScrollBar()->value();
    const QList<int> breakpoints = m_breakpoints.value(editor->path());
    // Replacing through a cursor keeps the reload on the undo stack.
    QTextCursor all(editor->document());
    all.select(QTextCursor::Document);
    all.insertText(QString::fromUtf8(bytes));
    editor->diskBytes = bytes;
    editor->changedOnDisk = false;
    editor->document()->setModified(false);
    editor->setBreakpoints(breakpoints);
    syncBreakpoints(editor);
    editor->goToLine(line + 1, column + 1);
    editor->verticalScrollBar()->setValue(scroll);
    updateBanner();
}

void CodeEditorPanel::updateBanner()
{
    const CodeEditor* editor = currentEditor();
    const bool show = editor && editor->changedOnDisk;
    m_banner->setVisible(show);
    if (!show) return;
    const bool exists = QFileInfo::exists(editor->path());
    m_bannerText->setText(exists ? tr("This file changed on disk while you had unsaved edits.")
                                 : tr("This file was deleted on disk. Saving will create it again."));
    m_bannerReload->setVisible(exists);
}

void CodeEditorPanel::syncBreakpoints(CodeEditor* editor)
{
    const QList<int> lines = editor->breakpoints();
    if (lines.isEmpty()) m_breakpoints.remove(editor->path());
    else m_breakpoints.insert(editor->path(), lines);
    LuaDebugger::Instance().SetBreakpoints(editor->path().toStdString(), std::vector<int>(lines.begin(), lines.end()));
}

void CodeEditorPanel::ShowExecutionLine(const QString& absolutePath, int line)
{
    if (CodeEditor* previous = editorForPath(m_executionPath)) previous->setExecutionLine(0);
    m_executionPath = absolutePath.isEmpty() ? QString() : QFileInfo(absolutePath).canonicalFilePath();
    if (m_executionPath.isEmpty() || !OpenFileAt(m_executionPath, line)) {
        m_executionPath.clear();
        return;
    }
    if (CodeEditor* editor = editorForPath(m_executionPath)) editor->setExecutionLine(line);
}

namespace {

QString SessionKey()
{
    const QByteArray root = QByteArray::fromStdString(AssetManager::GetInstance().GetProjectRoot());
    if (root.isEmpty()) return {};
    return QStringLiteral("codeEditor/") + QString::fromLatin1(QCryptographicHash::hash(root, QCryptographicHash::Md5).toHex());
}

} // namespace

void CodeEditorPanel::SaveSession()
{
    const QString key = SessionKey();
    if (key.isEmpty()) return;
    QStringList tabs;
    QStringList cursors;
    for (int i = 0; i < m_tabs->count(); ++i) {
        CodeEditor* editor = editorAt(i);
        syncBreakpoints(editor);
        tabs << editor->path();
        const QTextCursor cursor = editor->textCursor();
        cursors << QStringLiteral("%1:%2:%3").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1)
                       .arg(editor->verticalScrollBar()->value());
    }
    QVariantMap breakpoints;
    for (auto it = m_breakpoints.cbegin(); it != m_breakpoints.cend(); ++it) {
        QVariantList lines;
        for (int line : it.value()) lines << line;
        breakpoints.insert(it.key(), lines);
    }
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    settings.beginGroup(key);
    settings.setValue(QStringLiteral("tabs"), tabs);
    settings.setValue(QStringLiteral("cursors"), cursors);
    settings.setValue(QStringLiteral("current"), m_tabs->currentIndex());
    settings.setValue(QStringLiteral("breakpoints"), breakpoints);
    settings.endGroup();
}

void CodeEditorPanel::RestoreSession()
{
    for (auto it = m_breakpoints.cbegin(); it != m_breakpoints.cend(); ++it) {
        LuaDebugger::Instance().SetBreakpoints(it.key().toStdString(), {});
    }
    m_breakpoints.clear();
    const QString key = SessionKey();
    if (key.isEmpty()) return;
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    settings.beginGroup(key);
    const QVariantMap breakpoints = settings.value(QStringLiteral("breakpoints")).toMap();
    for (auto it = breakpoints.cbegin(); it != breakpoints.cend(); ++it) {
        if (!QFileInfo::exists(it.key())) continue;
        QList<int> lines;
        for (const QVariant& line : it.value().toList()) lines << line.toInt();
        m_breakpoints.insert(it.key(), lines);
        LuaDebugger::Instance().SetBreakpoints(it.key().toStdString(), std::vector<int>(lines.begin(), lines.end()));
    }
    const QStringList tabs = settings.value(QStringLiteral("tabs")).toStringList();
    const QStringList cursors = settings.value(QStringLiteral("cursors")).toStringList();
    const int current = settings.value(QStringLiteral("current"), -1).toInt();
    settings.endGroup();

    QString currentPath;
    for (int i = 0; i < tabs.size(); ++i) {
        if (!QFileInfo::exists(tabs[i]) || !OpenFile(tabs[i])) continue;
        if (i == current) currentPath = tabs[i];
        const QStringList parts = cursors.value(i).split(QLatin1Char(':'));
        if (CodeEditor* editor = currentEditor(); editor && parts.size() == 3) {
            editor->goToLine(parts[0].toInt(), parts[1].toInt());
            editor->verticalScrollBar()->setValue(parts[2].toInt());
        }
    }
    if (!currentPath.isEmpty()) OpenFile(currentPath);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
