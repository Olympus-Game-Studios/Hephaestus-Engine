#if HEPHAESTUS_EDITOR_QT_UI

#include "ProjectSearchPanel.h"
#include "EditorTheme.h"
#include "../../Assets/AssetManager.h"
#include "../../Scripting/ScriptLanguageRegistry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSet>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
constexpr int kColumnRole = Qt::UserRole + 3;
constexpr int kMaxMatches = 2000;
constexpr qint64 kMaxFileBytes = 2 * 1024 * 1024;

bool IsSearchable(const QFileInfo& file)
{
    static const QSet<QString> kTextSuffixes = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("json"), QStringLiteral("ini"),
        QStringLiteral("cfg"), QStringLiteral("xml"), QStringLiteral("csv"),
    };
    return ScriptLanguageRegistry::Instance().IsScriptPath(file.fileName().toStdString())
        || !Theme::FileIcon(file.fileName()).isNull() || kTextSuffixes.contains(file.suffix().toLower());
}

bool IsSkippedFolder(const QString& name)
{
    static const QSet<QString> kSkipped = {
        QStringLiteral(".git"), QStringLiteral(".hephaestus"), QStringLiteral("build"),
        QStringLiteral("Build"), QStringLiteral("Packages"), QStringLiteral("node_modules"),
    };
    return kSkipped.contains(name);
}

} // namespace

ProjectSearchPanel::ProjectSearchPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ProjectSearchPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("ConsoleBar"));
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);
    m_query = new QLineEdit(bar);
    m_query->setPlaceholderText(tr("Search project files (Enter)"));
    m_query->setClearButtonEnabled(true);
    m_query->addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
    row->addWidget(m_query, 1);
    auto toggle = [bar, row](const QString& text, const QString& tip) {
        auto* button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("CodeBarButton"));
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button);
        return button;
    };
    m_matchCase = toggle(QStringLiteral("Aa"), tr("Match case"));
    m_wholeWord = toggle(QStringLiteral("ab"), tr("Whole word"));
    m_useRegex = toggle(QStringLiteral(".*"), tr("Regular expression"));
    m_summary = new QLabel(bar);
    m_summary->setProperty("role", "muted");
    row->addWidget(m_summary);
    layout->addWidget(bar);

    m_results = new QTreeWidget(this);
    m_results->setObjectName(QStringLiteral("SearchResults"));
    m_results->setColumnCount(2);
    m_results->setHeaderHidden(true);
    m_results->setUniformRowHeights(true);
    m_results->header()->setStretchLastSection(true);
    m_results->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(m_results, 1);

    connect(m_query, &QLineEdit::returnPressed, this, &ProjectSearchPanel::Search);
    for (QToolButton* button : { m_matchCase, m_wholeWord, m_useRegex }) {
        connect(button, &QToolButton::toggled, this, [this]() {
            if (!m_query->text().isEmpty()) Search();
        });
    }
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, kPathRole).toString();
        if (!path.isEmpty() && onOpen) {
            onOpen(path, item->data(0, kLineRole).toInt(), item->data(0, kColumnRole).toInt());
        }
    });
}

void ProjectSearchPanel::FocusSearch(const QString& text)
{
    if (!text.isEmpty()) m_query->setText(text);
    m_query->setFocus(Qt::ShortcutFocusReason);
    m_query->selectAll();
}

void ProjectSearchPanel::Search()
{
    m_results->clear();
    const QString text = m_query->text();
    const QString root = QString::fromStdString(AssetManager::GetInstance().GetProjectRoot());
    if (text.isEmpty() || root.isEmpty()) {
        m_summary->setText(root.isEmpty() ? tr("Open a project to search it") : QString());
        return;
    }
    QString pattern = m_useRegex->isChecked() ? text : QRegularExpression::escape(text);
    if (m_wholeWord->isChecked()) pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
    const QRegularExpression expression(pattern, m_matchCase->isChecked() ? QRegularExpression::NoPatternOption
                                                                          : QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid()) {
        m_summary->setText(tr("Invalid regular expression"));
        return;
    }

    int matches = 0;
    int files = 0;
    QStringList pending{ root };
    while (!pending.isEmpty() && matches < kMaxMatches) {
        const QDir dir(pending.takeFirst());
        for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            if (entry.isDir()) {
                // Links and junctions can point back up the tree, so the walk only follows real folders.
                if (!entry.isSymbolicLink() && !entry.isJunction() && !IsSkippedFolder(entry.fileName())) {
                    pending << entry.filePath();
                }
                continue;
            }
            if (!IsSearchable(entry) || entry.size() > kMaxFileBytes) continue;
            QFile file(entry.filePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            QTreeWidgetItem* fileItem = nullptr;
            int fileMatches = 0;
            for (int i = 0; i < lines.size() && matches < kMaxMatches; ++i) {
                const QRegularExpressionMatch match = expression.match(lines[i]);
                if (!match.hasMatch() || match.capturedLength() == 0) continue;
                if (!fileItem) {
                    fileItem = new QTreeWidgetItem(m_results, { QDir(root).relativeFilePath(entry.filePath()) });
                    const QIcon icon = Theme::FileIcon(entry.fileName());
                    fileItem->setIcon(0, icon.isNull() ? Theme::Icon(QStringLiteral("file")) : icon);
                    fileItem->setFirstColumnSpanned(true);
                    ++files;
                }
                auto* hit = new QTreeWidgetItem(fileItem, { QString::number(i + 1), lines[i].trimmed() });
                hit->setForeground(0, Theme::Color(Theme::Token::TextMuted));
                hit->setData(0, kPathRole, entry.canonicalFilePath());
                hit->setData(0, kLineRole, i + 1);
                hit->setData(0, kColumnRole, int(match.capturedStart()) + 1);
                hit->setToolTip(1, lines[i]);
                ++fileMatches;
                ++matches;
            }
            if (fileItem) {
                fileItem->setText(0, fileItem->text(0) + QStringLiteral("  (%1)").arg(fileMatches));
                fileItem->setExpanded(true);
            }
        }
    }
    if (matches == 0) m_summary->setText(tr("No results"));
    else if (matches >= kMaxMatches) m_summary->setText(tr("First %1 results in %2 files").arg(matches).arg(files));
    else m_summary->setText(tr("%1 results in %2 files").arg(matches).arg(files));
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
