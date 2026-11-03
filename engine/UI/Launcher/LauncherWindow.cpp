#include "LauncherWindow.h"

#include "../QtEditor/EditorTheme.h"
#include "../../ToolsCommon/PathUtils.h"
#include "../../ToolsCommon/VersionInfo.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include <iterator>

namespace Hephaestus::Launcher {

namespace Theme = Hephaestus::QtEditor::Theme;
using Theme::Token;

namespace {

constexpr int kMaxVisibleProjectRows = 6;

const QString kDocumentationUrl = QStringLiteral("https://github.com/Olympus-Game-Studios/Hephaestus-Engine/tree/main/Documentation");
const QString kReleaseNotesUrl = QStringLiteral("https://github.com/Olympus-Game-Studios/Hephaestus-Engine/blob/main/CHANGELOG.md");

struct ModeCard {
    const char* id;
    const char* icon;
    const char* title;
    const char* description;
};

// Ids are the project file's "editorMode" values; icons match the editor's mode switch.
constexpr ModeCard kModeCards[] = {
    { "default", "layout", QT_TRANSLATE_NOOP("LauncherWindow", "Default"),
      QT_TRANSLATE_NOOP("LauncherWindow", "Balanced, familiar layout for everyone.") },
    { "artist", "paint", QT_TRANSLATE_NOOP("LauncherWindow", "Artist"),
      QT_TRANSLATE_NOOP("LauncherWindow", "Viewport first, tools on a rail, large thumbnails.") },
    { "programmer", "code", QT_TRANSLATE_NOOP("LauncherWindow", "Programmer"),
      QT_TRANSLATE_NOOP("LauncherWindow", "An IDE inside the engine with a live game view.") },
};

QPixmap LogoWithGlow(int size, int logoSize, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor glow = Theme::Color(Token::AccentBrand);
    QRadialGradient gradient(QPointF(size / 2.0, size / 2.0), size / 2.0);
    glow.setAlphaF(0.32);
    gradient.setColorAt(0.0, glow);
    glow.setAlphaF(0.12);
    gradient.setColorAt(0.55, glow);
    glow.setAlphaF(0.0);
    gradient.setColorAt(1.0, glow);
    painter.fillRect(QRectF(0, 0, size, size), gradient);

    const int offset = (size - logoSize) / 2;
    Theme::BrandLogo().paint(&painter, QRect(offset, offset, logoSize, logoSize));
    return pixmap;
}

QLabel* MakeLabel(const QString& text, const char* objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(objectName));
    return label;
}

QLabel* MakeLink(const QString& text, const QString& url, QWidget* parent)
{
    auto* link = new QLabel(QStringLiteral("<a href=\"%1\" style=\"text-decoration: none;\">%2</a>").arg(url, text.toHtmlEscaped()), parent);
    link->setObjectName(QStringLiteral("LauncherLink"));
    link->setOpenExternalLinks(true);
    link->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    return link;
}

} // namespace

LauncherWindow::LauncherWindow(QWidget* parent)
    : QWidget(parent)
    , m_projectManager(this)
    , m_projectScanner(this)
    , m_projectModel(this)
    , m_projectDelegate(this)
{
    setWindowTitle(Hephaestus::Tools::VersionInfo::AppName());
    setMinimumSize(1080, 680);
    resize(1280, 800);

    m_settings = m_projectManager.LoadSettings();

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(BuildIntroPanel());
    root->addWidget(BuildProjectsPanel(), 1);

    connect(&m_projectScanner, &LauncherProjectScanner::ScanFinished, this, &LauncherWindow::OnProjectsScanned);

    const QVector<LauncherProject> cachedProjects = m_projectManager.LoadProjectIndex();
    m_projectModel.SetProjects(cachedProjects);
    UpdateProjectListState();
    SelectLastOpenedProject();

    m_projectRescanTimer = new QTimer(this);
    m_projectRescanTimer->setInterval(5000);
    connect(m_projectRescanTimer, &QTimer::timeout, this, &LauncherWindow::RefreshProjectsAsync);
    m_projectRescanTimer->start();

    RefreshProjectsAsync();

    // One-shot: honor "Open last project on startup" after the window is shown.
    if (m_settings.openLastProjectOnStartup && !m_settings.lastOpenedProject.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            const int row = m_projectModel.RowForProjectPath(m_settings.lastOpenedProject);
            if (row >= 0) {
                OpenProject(m_projectModel.index(row, 0));
            }
        });
    }
}

QWidget* LauncherWindow::BuildIntroPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("LauncherIntro"));
    panel->setFixedWidth(440);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(48, 32, 48, 44);
    layout->setSpacing(0);

    auto* logo = new QLabel(panel);
    logo->setPixmap(LogoWithGlow(192, 104, devicePixelRatioF()));
    logo->setAccessibleName(Hephaestus::Tools::VersionInfo::AppName());
    layout->addWidget(logo, 0, Qt::AlignLeft);
    layout->addSpacing(12);

    layout->addWidget(MakeLabel(Hephaestus::Tools::VersionInfo::AppName(), "LauncherTitle", panel));
    layout->addSpacing(6);
    auto* version = MakeLabel(tr("Version %1").arg(Hephaestus::Tools::VersionInfo::EngineVersion()), "LauncherVersion", panel);
    version->setProperty("role", QStringLiteral("caption"));
    layout->addWidget(version);
    layout->addSpacing(20);

    auto* tagline = MakeLabel(tr("Creativity before complexity. Pick up where you left off, or start something new."),
                              "LauncherTagline", panel);
    tagline->setWordWrap(true);
    layout->addWidget(tagline);
    layout->addSpacing(24);

    auto* newButton = new QPushButton(Theme::Icon(QStringLiteral("plus"), Theme::IconTone::OnAction), tr("New project"), panel);
    newButton->setProperty("variant", QStringLiteral("primary"));
    auto* openButton = new QPushButton(Theme::Icon(QStringLiteral("folder")), tr("Open project..."), panel);
    for (QPushButton* button : { newButton, openButton }) {
        button->setObjectName(QStringLiteral("LauncherAction"));
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
        layout->addSpacing(8);
    }
    layout->addStretch(1);

    auto* links = new QHBoxLayout();
    links->setSpacing(16);
    links->addWidget(MakeLink(tr("Documentation"), kDocumentationUrl, panel));
    links->addWidget(MakeLink(tr("Release notes"), kReleaseNotesUrl, panel));
    links->addStretch(1);
    layout->addLayout(links);

    connect(newButton, &QPushButton::clicked, this, &LauncherWindow::CreateNewProject);
    connect(openButton, &QPushButton::clicked, this, &LauncherWindow::OpenProjectFromDisk);
    return panel;
}

QWidget* LauncherWindow::BuildProjectsPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("LauncherProjects"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(48, 44, 48, 44);
    layout->setSpacing(0);

    auto* header = new QHBoxLayout();
    header->addWidget(MakeLabel(tr("Recent projects"), "LauncherSectionTitle", panel));
    header->addStretch(1);
    m_projectSearchEdit = new QLineEdit(panel);
    m_projectSearchEdit->setObjectName(QStringLiteral("ProjectSearch"));
    m_projectSearchEdit->setPlaceholderText(tr("Search projects..."));
    m_projectSearchEdit->setFixedWidth(260);
    m_projectSearchEdit->addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
    Theme::AddShortcutHint(m_projectSearchEdit, QKeySequence(QKeySequence::Find));
    header->addWidget(m_projectSearchEdit);
    layout->addLayout(header);
    layout->addSpacing(14);

    m_projectList = new QListView(panel);
    m_projectList->setObjectName(QStringLiteral("RecentProjects"));
    m_projectList->setModel(&m_projectModel);
    m_projectList->setItemDelegate(&m_projectDelegate);
    m_projectList->setMouseTracking(true);
    m_projectList->setUniformItemSizes(true);
    m_projectList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_projectList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_projectList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_projectList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_projectList->setAccessibleName(tr("Recent projects"));
    layout->addWidget(m_projectList);

    m_emptyStateLabel = MakeLabel(QString(), "RecentProjectsEmpty", panel);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setWordWrap(true);
    layout->addWidget(m_emptyStateLabel);
    layout->addSpacing(40);

    layout->addWidget(MakeLabel(tr("How do you want to work?"), "LauncherSectionTitle", panel));
    layout->addSpacing(10);
    auto* modeCaption = MakeLabel(tr("New projects open in this mode. You can switch at any time from the mode switcher in the header."),
                                  "LauncherModeCaption", panel);
    modeCaption->setProperty("role", QStringLiteral("muted"));
    modeCaption->setWordWrap(true);
    layout->addWidget(modeCaption);
    layout->addSpacing(14);
    layout->addWidget(BuildModeCards(panel));
    layout->addStretch(1);

    connect(m_projectSearchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_projectModel.SetSearchText(text);
        UpdateProjectListState();
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        m_projectSearchEdit->setFocus(Qt::ShortcutFocusReason);
        m_projectSearchEdit->selectAll();
    });
    connect(m_projectList, &QListView::activated, this, &LauncherWindow::OpenProject);
    connect(m_projectList, &QWidget::customContextMenuRequested, this, &LauncherWindow::ShowProjectContextMenu);
    return panel;
}

QWidget* LauncherWindow::BuildModeCards(QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("ModeCards"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(16);

    m_modeButtons = new QButtonGroup(this);
    for (int i = 0; i < static_cast<int>(std::size(kModeCards)); ++i) {
        const ModeCard& mode = kModeCards[i];
        auto* card = new QPushButton(row);
        card->setObjectName(QStringLiteral("ModeCard"));
        card->setCheckable(true);
        card->setCursor(Qt::PointingHandCursor);
        card->setAccessibleName(tr(mode.title));
        card->setAccessibleDescription(tr(mode.description));

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(17, 15, 17, 15);
        cardLayout->setSpacing(8);
        auto* titleRow = new QHBoxLayout();
        titleRow->setSpacing(8);
        auto* icon = new QLabel(card);
        auto* check = new QLabel(card);
        check->setPixmap(Theme::Icon(QStringLiteral("check"), Theme::IconTone::Action).pixmap(16, 16));
        titleRow->addWidget(icon);
        titleRow->addWidget(MakeLabel(tr(mode.title), "ModeCardTitle", card));
        titleRow->addStretch(1);
        titleRow->addWidget(check);
        cardLayout->addLayout(titleRow);
        auto* description = MakeLabel(tr(mode.description), "ModeCardDescription", card);
        description->setWordWrap(true);
        cardLayout->addWidget(description);
        cardLayout->addStretch(1);
        for (QLabel* label : card->findChildren<QLabel*>()) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }

        const bool chosen = m_settings.newProjectMode == QLatin1String(mode.id);
        card->setChecked(chosen);
        check->setVisible(chosen);
        icon->setPixmap(Theme::Icon(QString::fromLatin1(mode.icon), chosen ? Theme::IconTone::Action : Theme::IconTone::Secondary).pixmap(16, 16));
        connect(card, &QPushButton::toggled, this, [check, icon, iconName = QString::fromLatin1(mode.icon)](bool on) {
            check->setVisible(on);
            icon->setPixmap(Theme::Icon(iconName, on ? Theme::IconTone::Action : Theme::IconTone::Secondary).pixmap(16, 16));
        });

        m_modeButtons->addButton(card, i);
        rowLayout->addWidget(card, 1);
    }

    if (!m_modeButtons->checkedButton()) {
        m_modeButtons->button(0)->setChecked(true);
    }
    connect(m_modeButtons, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.newProjectMode = QLatin1String(kModeCards[id].id);
        m_projectManager.SaveSettings(m_settings);
    });
    return row;
}

void LauncherWindow::RefreshProjectsAsync()
{
    if (m_projectScanner.IsScanning()) {
        return;
    }
    m_projectScanner.StartScan(m_settings.projectRootFolders);
}

void LauncherWindow::OnProjectsScanned(const QVector<LauncherProject>& projects)
{
    const QString selectedPath = m_projectList->currentIndex().data(ProjectCardModel::ProjectPathRole).toString();

    m_projectModel.SetProjects(projects);
    m_projectManager.SaveProjectIndex(projects);
    UpdateProjectListState();

    const int row = selectedPath.isEmpty() ? -1 : m_projectModel.RowForProjectPath(selectedPath);
    if (row >= 0) {
        m_projectList->setCurrentIndex(m_projectModel.index(row, 0));
    } else {
        SelectLastOpenedProject();
    }
}

void LauncherWindow::SelectLastOpenedProject()
{
    const int row = m_projectModel.RowForProjectPath(m_settings.lastOpenedProject);
    if (row >= 0) {
        m_projectList->setCurrentIndex(m_projectModel.index(row, 0));
    }
}

void LauncherWindow::UpdateProjectListState()
{
    const int rows = m_projectModel.rowCount();
    m_projectList->setVisible(rows > 0);
    m_emptyStateLabel->setVisible(rows == 0);
    if (rows > 0) {
        m_projectList->setFixedHeight(qMin(rows, kMaxVisibleProjectRows) * ProjectCardDelegate::kRowHeight
                                      + 2 * m_projectList->frameWidth());
        return;
    }

    const QString search = m_projectSearchEdit->text().trimmed();
    m_emptyStateLabel->setText(search.isEmpty()
        ? tr("No projects yet. Create a new project or open one from disk.")
        : tr("No projects match \"%1\".").arg(search));
}

void LauncherWindow::ShowProjectContextMenu(const QPoint& position)
{
    const QModelIndex index = m_projectList->indexAt(position);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction* open = menu.addAction(tr("Open"));
    open->setEnabled(!index.data(ProjectCardModel::BrokenRole).toBool());
    QAction* reveal = menu.addAction(tr("Show in Explorer"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete from disk..."));

    QAction* chosen = menu.exec(m_projectList->viewport()->mapToGlobal(position));
    if (chosen == open) {
        OpenProject(index);
    } else if (chosen == reveal) {
        ShowProjectInExplorer(index);
    } else if (chosen == remove) {
        DeleteProject(index);
    }
}

void LauncherWindow::OpenProject(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    const LauncherProject project = m_projectModel.ProjectAt(index);
    if (project.broken) {
        QMessageBox::warning(this,
            tr("Broken project"),
            tr("This project has invalid metadata and cannot be opened until fixed."));
        return;
    }
    LaunchEditor(project);
}

void LauncherWindow::OpenProjectFromDisk()
{
    const QString startFolder = m_settings.projectRootFolders.isEmpty() ? QDir::homePath() : m_settings.projectRootFolders.front();
    const QString projectFile = QFileDialog::getOpenFileName(this, tr("Open project"), startFolder,
                                                             tr("Hephaestus project (*.heproject)"));
    if (projectFile.isEmpty()) {
        return;
    }

    LauncherProject project;
    if (!m_projectManager.ReadProject(projectFile, &project)) {
        QMessageBox::warning(this, tr("Open project"), tr("Could not read the project file:\n%1").arg(projectFile));
        return;
    }

    QDir parent(project.projectDir);
    if (parent.cdUp()) {
        AddProjectRoot(parent.absolutePath());
    }
    LaunchEditor(project);
}

void LauncherWindow::LaunchEditor(const LauncherProject& project)
{
    const QString editorPath = EnsureExecutableExistsOrPrompt(m_settings.editorPath, tr("Select Editor Executable"));
    if (editorPath.isEmpty()) {
        return;
    }

    m_settings.editorPath = editorPath;
    m_settings.lastOpenedProject = project.projectDir;
    m_projectManager.SaveSettings(m_settings);

    QString error;
    if (!m_projectManager.OpenProjectInEditor(project, editorPath, &error)) {
        QMessageBox::critical(this, tr("Open failed"), error);
        return;
    }

    close();
}

void LauncherWindow::DeleteProject(const QModelIndex& index)
{
    const LauncherProject project = m_projectModel.ProjectAt(index);
    if (project.projectDir.isEmpty()) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        tr("Delete project"),
        tr("Delete project '%1' and all files on disk?\n\nThis cannot be undone.").arg(project.name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_projectManager.DeleteProject(project, &error)) {
        QMessageBox::critical(this, tr("Delete failed"), error);
        return;
    }

    RefreshProjectsAsync();
}

void LauncherWindow::ShowProjectInExplorer(const QModelIndex& index)
{
    const LauncherProject project = m_projectModel.ProjectAt(index);
    if (!project.projectDir.isEmpty() && !m_projectManager.OpenInExplorer(project.projectDir)) {
        QMessageBox::warning(this, tr("Explorer"), tr("Failed to open project folder."));
    }
}

void LauncherWindow::CreateNewProject()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New project"));
    dialog.setMinimumWidth(480);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(14);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    auto* nameEdit = new QLineEdit(QStringLiteral("MyHephaestusProject"), &dialog);
    auto* locationEdit = new QLineEdit(m_settings.projectRootFolders.isEmpty() ? QDir::homePath()
                                                                               : m_settings.projectRootFolders.front(), &dialog);
    auto* browseButton = new QPushButton(tr("Browse..."), &dialog);
    auto* locationRow = new QHBoxLayout();
    locationRow->setSpacing(8);
    locationRow->addWidget(locationEdit, 1);
    locationRow->addWidget(browseButton);
    auto* defaultSceneCheck = new QCheckBox(tr("Create a default scene"), &dialog);
    defaultSceneCheck->setChecked(true);
    auto* pathPreview = new QLabel(&dialog);
    pathPreview->setProperty("role", QStringLiteral("caption"));
    pathPreview->setWordWrap(true);

    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Location"), locationRow);
    form->addRow(QString(), defaultSceneCheck);
    form->addRow(QString(), pathPreview);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* createButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    createButton->setDefault(true);
    layout->addWidget(buttons);

    const auto updatePreview = [&]() {
        const QString name = nameEdit->text().trimmed();
        const QString location = locationEdit->text().trimmed();
        const bool valid = !name.isEmpty() && !location.isEmpty();
        createButton->setEnabled(valid);
        pathPreview->setText(valid ? QDir::toNativeSeparators(QDir(location).filePath(name)) : QString());
    };
    updatePreview();
    connect(nameEdit, &QLineEdit::textChanged, &dialog, updatePreview);
    connect(locationEdit, &QLineEdit::textChanged, &dialog, updatePreview);
    connect(browseButton, &QPushButton::clicked, &dialog, [&]() {
        const QString selected = QFileDialog::getExistingDirectory(&dialog, tr("Select Project Location"), locationEdit->text().trimmed());
        if (!selected.isEmpty()) {
            locationEdit->setText(selected);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString location = locationEdit->text().trimmed();
    QString error;
    if (!m_projectManager.CreateProject(
            nameEdit->text().trimmed(),
            location,
            Hephaestus::Tools::VersionInfo::EngineVersion(),
            defaultSceneCheck->isChecked(),
            m_settings.newProjectMode,
            nullptr,
            &error)) {
        QMessageBox::warning(this, tr("Create project"), error);
        return;
    }

    AddProjectRoot(location);
    RefreshProjectsAsync();
}

void LauncherWindow::AddProjectRoot(const QString& folder)
{
    for (const QString& root : m_settings.projectRootFolders) {
        if (QDir::cleanPath(root) == QDir::cleanPath(folder)) {
            return;
        }
    }
    m_settings.projectRootFolders.push_back(folder);
    m_projectManager.SaveSettings(m_settings);
}

QString LauncherWindow::EnsureExecutableExistsOrPrompt(const QString& currentPath, const QString& dialogTitle)
{
    const QFileInfo info(currentPath);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }

    QMessageBox::warning(this,
        tr("Executable not found"),
        tr("%1\n\nPlease choose the executable path.").arg(currentPath));

    return QFileDialog::getOpenFileName(this,
        dialogTitle,
        Hephaestus::Tools::PathUtils::ExecutableDir(),
#if defined(Q_OS_WIN)
        tr("Executable (*.exe)")
#else
        tr("Executable (*)")
#endif
    );
}

} // namespace Hephaestus::Launcher
