#include "LauncherWindow.h"
#include "../Shared/SettingsWindow.h"

#include "../../ToolsCommon/PathUtils.h"
#include "../../ToolsCommon/VersionInfo.h"
#include "../../ToolsCommon/EngineContext.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QStandardItemModel>
#include <QFormLayout>
#include <QFrame>
#include <QTimer>
#include <QVBoxLayout>
#include <QModelIndex>

namespace {

QString BuiltinMinimalDarkQss()
{
    return QString::fromUtf8(R"(
QWidget {
    background-color: #20242b;
    color: #d8dee9;
    font-size: 13px;
}
QLineEdit {
    background-color: #1a1e24;
    border: 1px solid #3b4252;
    border-radius: 4px;
    padding: 6px;
}
QPushButton {
    background-color: #3b4252;
    border: 1px solid #4c566a;
    border-radius: 4px;
    padding: 6px 10px;
}
QPushButton:hover {
    background-color: #4c566a;
}
QPushButton:pressed {
    background-color: #2e3440;
}
QLabel#status {
    color: #88c0d0;
}
)");
}

QString PickFirstExisting(const QStringList& candidates)
{
    for (const QString& c : candidates) {
        const QFileInfo info(c);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QPushButton* CreateSidebarButton(const QIcon& icon, const QString& text)
{
    auto* button = new QPushButton(icon, text);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setObjectName(QStringLiteral("SidebarButton"));
    return button;
}

QFrame* BuildSeparator()
{
    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setObjectName(QStringLiteral("ThinSeparator"));
    line->setFixedHeight(1);
    return line;
}

} // namespace

namespace Hephaestus::Launcher {

LauncherWindow::LauncherWindow(QWidget* parent)
    : QWidget(parent)
    , m_assets(Hephaestus::Tools::SharedAssetPaths::Locate())
    , m_projectManager(this)
    , m_projectScanner(this)
    , m_projectModel(this)
    , m_projectDelegate(this)
{
    m_engineSettingsManager.Load();

    setWindowTitle(QStringLiteral("Hephaestus Project Browser"));
    setMinimumSize(1320, 780);

    BuildUiRoot();
    BindSignals();
    ApplyTheme();
    LoadSettings();

    const QVector<LauncherProject> cachedProjects = m_projectManager.LoadProjectIndex();
    if (!cachedProjects.isEmpty()) {
        m_projectModel.SetProjects(cachedProjects);
        UpdateRecentEmptyState();
        UpdateProjectDetailsPanel();
        m_scanStatusLabel->setText(QStringLiteral("Loaded %1 cached projects. Confirming...").arg(cachedProjects.size()));

        if (!m_settings.lastOpenedProject.isEmpty()) {
            const int row = m_projectModel.RowForProjectPath(m_settings.lastOpenedProject);
            if (row >= 0) {
                const QModelIndex idx = m_projectModel.index(row, 0);
                m_projectGrid->setCurrentIndex(idx);
            }
        }
    }

    m_projectRescanTimer = new QTimer(this);
    m_projectRescanTimer->setInterval(5000);
    connect(m_projectRescanTimer, &QTimer::timeout, this, &LauncherWindow::RefreshProjectsAsync);
    m_projectRescanTimer->start();

    RefreshProjectsAsync();
}

void LauncherWindow::BuildUiRoot()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    BuildSidebar();
    BuildPages();

    auto* sidebarContainer = m_sidebar->parentWidget();
    root->addWidget(sidebarContainer);
    root->addWidget(m_mainContent, 1);
}

void LauncherWindow::BuildSidebar()
{
    auto* sidebarContainer = new QWidget(this);
    sidebarContainer->setObjectName(QStringLiteral("LauncherSidebar"));
    sidebarContainer->setFixedWidth(240);

    auto* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(12, 14, 12, 14);
    sidebarLayout->setSpacing(10);

    auto* brand = new QLabel(QStringLiteral("Hephaestus\nProject Browser"), sidebarContainer);
    brand->setObjectName(QStringLiteral("SidebarBrand"));
    sidebarLayout->addWidget(brand);
    sidebarLayout->addWidget(BuildSeparator());

    m_sidebar = new QListWidget(sidebarContainer);
    m_sidebar->setObjectName(QStringLiteral("SidebarList"));
    m_sidebar->setSpacing(4);
    m_sidebar->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* recentItem = new QListWidgetItem(LoadIcon(QStringLiteral("icons/folder.png")), QStringLiteral("Recent Projects"));
    auto* newItem = new QListWidgetItem(LoadIcon(QStringLiteral("icons/plus.svg")), QStringLiteral("New Project"));
    auto* templatesItem = new QListWidgetItem(LoadIcon(QStringLiteral("icons/file-svg.png")), QStringLiteral("Templates"));
    auto* settingsItem = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("Settings"));

    templatesItem->setFlags(templatesItem->flags() & ~Qt::ItemIsEnabled);

    m_sidebar->addItem(recentItem);
    m_sidebar->addItem(newItem);
    m_sidebar->addItem(templatesItem);
    m_sidebar->addItem(settingsItem);
    m_sidebar->setCurrentRow(RecentProjectsPage);

    sidebarLayout->addWidget(m_sidebar, 1);
}

void LauncherWindow::BuildPages()
{
    m_mainContent = new QStackedWidget(this);
    m_mainContent->setObjectName(QStringLiteral("LauncherMainContent"));

    m_recentPage = BuildRecentProjectsPage();
    m_newProjectPage = BuildNewProjectPage();
    auto* templatesPage = BuildTemplatesPage();
    m_settingsPage = BuildSettingsPage();

    m_mainContent->addWidget(m_recentPage);
    m_mainContent->addWidget(m_newProjectPage);
    m_mainContent->addWidget(templatesPage);
    m_mainContent->addWidget(m_settingsPage);
    m_mainContent->setCurrentIndex(RecentProjectsPage);
}

QWidget* LauncherWindow::BuildRecentProjectsPage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(24, 24, 24, 18);
    root->setSpacing(12);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("Projects"), page);
    title->setObjectName(QStringLiteral("PageTitle"));
    m_openSettingsButton = new QPushButton(QStringLiteral("Settings"), page);
    m_openSettingsButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_projectSearchEdit = new QLineEdit(page);
    m_projectSearchEdit->setPlaceholderText(QStringLiteral("Filter Projects"));
    m_projectSearchEdit->setObjectName(QStringLiteral("ProjectSearch"));
    m_sortCombo = new QComboBox(page);
    m_sortCombo->addItems({QStringLiteral("Last Modified"), QStringLiteral("Name")});
    m_sortCombo->setObjectName(QStringLiteral("SortCombo"));

    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_openSettingsButton);
    header->addWidget(m_projectSearchEdit, 1);
    header->addWidget(m_sortCombo);
    root->addLayout(header);

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("ProjectsSplitter"));

    m_gridHostWidget = new QWidget(splitter);
    auto* gridHostLayout = new QVBoxLayout(m_gridHostWidget);
    gridHostLayout->setContentsMargins(0, 0, 0, 0);
    gridHostLayout->setSpacing(0);

    m_projectGrid = new QListView(m_gridHostWidget);
    m_projectGrid->setObjectName(QStringLiteral("ProjectGrid"));
    m_projectGrid->setModel(&m_projectModel);
    m_projectGrid->setItemDelegate(&m_projectDelegate);
    m_projectGrid->setMouseTracking(true);
    m_projectGrid->setViewMode(QListView::IconMode);
    m_projectGrid->setFlow(QListView::LeftToRight);
    m_projectGrid->setResizeMode(QListView::Adjust);
    m_projectGrid->setUniformItemSizes(true);
    m_projectGrid->setWordWrap(false);
    m_projectGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_projectGrid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_projectGrid->setSpacing(10);

    m_emptyStateWidget = new QWidget(m_gridHostWidget);
    m_emptyStateWidget->setObjectName(QStringLiteral("EmptyState"));
    auto* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyIcon = new QLabel(QStringLiteral("◈"), m_emptyStateWidget);
    emptyIcon->setObjectName(QStringLiteral("EmptyIcon"));
    auto* emptyText = new QLabel(QStringLiteral("No Projects Found"), m_emptyStateWidget);
    emptyText->setObjectName(QStringLiteral("EmptyTitle"));
    auto* emptyButton = new QPushButton(QStringLiteral("Create New Project"), m_emptyStateWidget);
    emptyButton->setObjectName(QStringLiteral("PrimaryButton"));

    emptyLayout->addWidget(emptyIcon, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(emptyText, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(emptyButton, 0, Qt::AlignHCenter);

    gridHostLayout->addWidget(m_projectGrid);
    gridHostLayout->addWidget(m_emptyStateWidget);

    auto* detailsPanel = new QWidget(splitter);
    detailsPanel->setObjectName(QStringLiteral("ProjectDetailsPanel"));
    detailsPanel->setMinimumWidth(320);
    auto* detailsLayout = new QVBoxLayout(detailsPanel);
    detailsLayout->setContentsMargins(14, 14, 14, 14);
    detailsLayout->setSpacing(10);

    auto* detailsTitle = new QLabel(QStringLiteral("Project Details"), detailsPanel);
    detailsTitle->setObjectName(QStringLiteral("SectionTitle"));
    detailsLayout->addWidget(detailsTitle);

    m_selectedNameLabel = new QLabel(detailsPanel);
    m_selectedNameLabel->setObjectName(QStringLiteral("DetailsName"));
    m_selectedDescriptionLabel = new QLabel(detailsPanel);
    m_selectedDescriptionLabel->setWordWrap(true);
    m_selectedCreatedLabel = new QLabel(detailsPanel);
    m_selectedModifiedLabel = new QLabel(detailsPanel);
    m_selectedPathLabel = new QLabel(detailsPanel);
    m_selectedPathLabel->setWordWrap(true);

    detailsLayout->addWidget(m_selectedNameLabel);
    detailsLayout->addWidget(m_selectedDescriptionLabel);
    detailsLayout->addWidget(m_selectedCreatedLabel);
    detailsLayout->addWidget(m_selectedModifiedLabel);
    detailsLayout->addWidget(m_selectedPathLabel);

    detailsLayout->addWidget(BuildSeparator());

    m_openButton = new QPushButton(QStringLiteral("Open"), detailsPanel);
    m_openButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_showExplorerButton = new QPushButton(QStringLiteral("Show in Explorer"), detailsPanel);
    m_duplicateButton = new QPushButton(QStringLiteral("Duplicate"), detailsPanel);
    m_duplicateButton->setEnabled(false);
    m_deleteButton = new QPushButton(QStringLiteral("Delete"), detailsPanel);
    m_deleteButton->setObjectName(QStringLiteral("DangerButton"));

    detailsLayout->addWidget(m_openButton);
    detailsLayout->addWidget(m_showExplorerButton);
    detailsLayout->addWidget(m_duplicateButton);
    detailsLayout->addWidget(m_deleteButton);
    detailsLayout->addStretch(1);

    splitter->addWidget(m_gridHostWidget);
    splitter->addWidget(detailsPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 360});
    root->addWidget(splitter, 1);

    auto* bottom = new QHBoxLayout();
    m_scanStatusLabel = new QLabel(QStringLiteral("Ready"), page);
    m_scanStatusLabel->setObjectName(QStringLiteral("status"));
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), page);
    m_openBottomButton = new QPushButton(QStringLiteral("Open"), page);
    m_openBottomButton->setObjectName(QStringLiteral("PrimaryButton"));
    bottom->addWidget(m_scanStatusLabel);
    bottom->addStretch(1);
    bottom->addWidget(cancelButton);
    bottom->addWidget(m_openBottomButton);
    root->addLayout(bottom);

    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);
    connect(emptyButton, &QPushButton::clicked, this, [this]() {
        NavigateToSidebarItem(NewProjectPage);
    });

    return page;
}

QWidget* LauncherWindow::BuildNewProjectPage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(24, 24, 24, 20);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Create New Project"), page);
    title->setObjectName(QStringLiteral("PageTitle"));
    root->addWidget(title);

    auto* content = new QHBoxLayout();
    content->setSpacing(18);

    auto* typePanel = new QWidget(page);
    typePanel->setObjectName(QStringLiteral("Panel"));
    typePanel->setMinimumWidth(280);
    auto* typeLayout = new QVBoxLayout(typePanel);
    auto* typeTitle = new QLabel(QStringLiteral("Project Type"), typePanel);
    typeTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_projectTypeList = new QListWidget(typePanel);
    m_projectTypeList->addItem(QStringLiteral("Blank Project"));
    auto* futureTemplate = new QListWidgetItem(QStringLiteral("More templates coming soon"));
    futureTemplate->setFlags(futureTemplate->flags() & ~Qt::ItemIsEnabled);
    m_projectTypeList->addItem(futureTemplate);
    m_projectTypeList->setCurrentRow(0);
    typeLayout->addWidget(typeTitle);
    typeLayout->addWidget(m_projectTypeList);

    auto* formPanel = new QWidget(page);
    formPanel->setObjectName(QStringLiteral("Panel"));
    auto* formLayout = new QVBoxLayout(formPanel);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_newProjectNameEdit = new QLineEdit(formPanel);
    m_newProjectLocationEdit = new QLineEdit(formPanel);
    auto* browseButton = new QPushButton(QStringLiteral("Browse"), formPanel);
    auto* locationRow = new QWidget(formPanel);
    auto* locationLayout = new QHBoxLayout(locationRow);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    locationLayout->addWidget(m_newProjectLocationEdit, 1);
    locationLayout->addWidget(browseButton);

    m_engineVersionCombo = new QComboBox(formPanel);
    m_engineVersionCombo->addItems({
        Hephaestus::Tools::VersionInfo::EngineVersion(),
        QStringLiteral("1.0.0")
    });
    m_createDefaultSceneCheck = new QCheckBox(QStringLiteral("Create default scene"), formPanel);
    m_createDefaultSceneCheck->setChecked(true);
    m_projectPathPreviewLabel = new QLabel(formPanel);
    m_projectPathPreviewLabel->setObjectName(QStringLiteral("PathPreview"));
    m_projectPathPreviewLabel->setWordWrap(true);

    form->addRow(QStringLiteral("Project Name"), m_newProjectNameEdit);
    form->addRow(QStringLiteral("Location"), locationRow);
    form->addRow(QStringLiteral("Engine Version"), m_engineVersionCombo);
    form->addRow(QStringLiteral(""), m_createDefaultSceneCheck);
    form->addRow(QStringLiteral("Project Path"), m_projectPathPreviewLabel);
    formLayout->addLayout(form);
    formLayout->addStretch(1);

    content->addWidget(typePanel);
    content->addWidget(formPanel, 1);
    root->addLayout(content, 1);

    auto* bottom = new QHBoxLayout();
    auto* backButton = new QPushButton(QStringLiteral("Back"), page);
    auto* createButton = new QPushButton(QStringLiteral("Create"), page);
    createButton->setObjectName(QStringLiteral("PrimaryButton"));
    bottom->addStretch(1);
    bottom->addWidget(backButton);
    bottom->addWidget(createButton);
    root->addLayout(bottom);

    connect(backButton, &QPushButton::clicked, this, [this]() {
        NavigateToSidebarItem(RecentProjectsPage);
    });
    connect(createButton, &QPushButton::clicked, this, &LauncherWindow::CreateNewProject);
    connect(browseButton, &QPushButton::clicked, this, &LauncherWindow::OnBrowseNewProjectLocation);
    connect(m_newProjectNameEdit, &QLineEdit::textChanged, this, &LauncherWindow::UpdateNewProjectPathPreview);
    connect(m_newProjectLocationEdit, &QLineEdit::textChanged, this, &LauncherWindow::UpdateNewProjectPathPreview);

    return page;
}

QWidget* LauncherWindow::BuildTemplatesPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    auto* title = new QLabel(QStringLiteral("Templates"), page);
    title->setObjectName(QStringLiteral("PageTitle"));
    auto* msg = new QLabel(QStringLiteral("Template packs will arrive in a future update."), page);
    msg->setObjectName(QStringLiteral("MutedLabel"));
    layout->addWidget(title);
    layout->addWidget(msg);
    layout->addStretch(1);
    return page;
}

QWidget* LauncherWindow::BuildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(24, 24, 24, 20);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Settings"), page);
    title->setObjectName(QStringLiteral("PageTitle"));
    root->addWidget(title);

    auto* panel = new QWidget(page);
    panel->setObjectName(QStringLiteral("Panel"));
    auto* form = new QFormLayout(panel);
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(12);

    m_editorPathEdit = new QLineEdit(panel);
    auto* browseEditor = new QPushButton(QStringLiteral("Browse"), panel);
    auto* editorRow = new QWidget(panel);
    auto* editorRowLayout = new QHBoxLayout(editorRow);
    editorRowLayout->setContentsMargins(0, 0, 0, 0);
    editorRowLayout->addWidget(m_editorPathEdit, 1);
    editorRowLayout->addWidget(browseEditor);

    m_runtimePathEdit = new QLineEdit(panel);
    auto* browseRuntime = new QPushButton(QStringLiteral("Browse"), panel);
    auto* runtimeRow = new QWidget(panel);
    auto* runtimeRowLayout = new QHBoxLayout(runtimeRow);
    runtimeRowLayout->setContentsMargins(0, 0, 0, 0);
    runtimeRowLayout->addWidget(m_runtimePathEdit, 1);
    runtimeRowLayout->addWidget(browseRuntime);

    m_projectRootsList = new QListWidget(panel);
    auto* rootsControls = new QWidget(panel);
    auto* rootsControlsLayout = new QHBoxLayout(rootsControls);
    rootsControlsLayout->setContentsMargins(0, 0, 0, 0);
    auto* addRootButton = new QPushButton(QStringLiteral("Add"), rootsControls);
    auto* removeRootButton = new QPushButton(QStringLiteral("Remove"), rootsControls);
    rootsControlsLayout->addWidget(addRootButton);
    rootsControlsLayout->addWidget(removeRootButton);
    rootsControlsLayout->addStretch(1);

    auto* rootsBlock = new QWidget(panel);
    auto* rootsBlockLayout = new QVBoxLayout(rootsBlock);
    rootsBlockLayout->setContentsMargins(0, 0, 0, 0);
    rootsBlockLayout->setSpacing(6);
    rootsBlockLayout->addWidget(m_projectRootsList);
    rootsBlockLayout->addWidget(rootsControls);

    m_themeCombo = new QComboBox(panel);
    m_themeCombo->addItems({QStringLiteral("Dark")});

    m_openLastProjectCheck = new QCheckBox(QStringLiteral("Open Last Project on Startup"), panel);

    form->addRow(QStringLiteral("Editor Path"), editorRow);
    form->addRow(QStringLiteral("Runtime Path"), runtimeRow);
    form->addRow(QStringLiteral("Project Root Folders"), rootsBlock);
    form->addRow(QStringLiteral("Theme"), m_themeCombo);
    form->addRow(QString(), m_openLastProjectCheck);

    root->addWidget(panel, 1);

    connect(browseEditor, &QPushButton::clicked, this, &LauncherWindow::OnBrowseEditorPath);
    connect(browseRuntime, &QPushButton::clicked, this, &LauncherWindow::OnBrowseRuntimePath);
    connect(addRootButton, &QPushButton::clicked, this, &LauncherWindow::OnAddProjectRoot);
    connect(removeRootButton, &QPushButton::clicked, this, &LauncherWindow::OnRemoveProjectRoot);

    return page;
}

void LauncherWindow::BindSignals()
{
    connect(m_sidebar, &QListWidget::currentRowChanged, this, &LauncherWindow::NavigateToSidebarItem);

    connect(m_projectSearchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_projectModel.SetSearchText(text);
        UpdateRecentEmptyState();
    });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_projectModel.SetSortMode(index == 1 ? ProjectSortMode::Name : ProjectSortMode::LastModified);
        UpdateRecentEmptyState();
    });

    connect(m_projectGrid->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() { OnProjectSelectionChanged(); });
    connect(m_projectGrid, &QListView::activated, this, [this](const QModelIndex& index) {
        OpenProject(index);
    });

    connect(&m_projectDelegate, &ProjectCardDelegate::FavoriteToggled, this,
            [this](const QModelIndex& index, bool favorite) {
                SetProjectFavorite(index, favorite);
            });

    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        OpenProject(CurrentProjectIndex());
    });
    connect(m_openBottomButton, &QPushButton::clicked, this, [this]() {
        OpenProject(CurrentProjectIndex());
    });
    connect(m_showExplorerButton, &QPushButton::clicked, this, &LauncherWindow::ShowProjectInExplorer);
    connect(m_deleteButton, &QPushButton::clicked, this, &LauncherWindow::DeleteSelectedProject);
    connect(m_openSettingsButton, &QPushButton::clicked, this, &LauncherWindow::OpenSettingsWindow);

    connect(&m_projectScanner, &LauncherProjectScanner::ScanStarted, this, [this]() {
        m_scanStatusLabel->setText(QStringLiteral("Scanning projects..."));
    });
    connect(&m_projectScanner, &LauncherProjectScanner::ScanFinished, this, &LauncherWindow::OnProjectsScanned);

    auto saveSettingsFn = [this]() {
        if (!m_isInitializingUi) {
            SaveSettings();
        }
    };

    connect(m_editorPathEdit, &QLineEdit::editingFinished, this, saveSettingsFn);
    connect(m_runtimePathEdit, &QLineEdit::editingFinished, this, saveSettingsFn);
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [saveSettingsFn](int) { saveSettingsFn(); });
    connect(m_openLastProjectCheck, &QCheckBox::stateChanged, this, [saveSettingsFn](int) { saveSettingsFn(); });
}

void LauncherWindow::LoadSettings()
{
    m_isInitializingUi = true;
    m_settings = m_projectManager.LoadSettings();

    m_editorPathEdit->setText(m_settings.editorPath);
    m_runtimePathEdit->setText(m_settings.runtimePath);

    m_projectRootsList->clear();
    for (const QString& root : m_settings.projectRootFolders) {
        m_projectRootsList->addItem(root);
    }

    const int themeIndex = m_themeCombo->findText(m_settings.theme);
    if (themeIndex >= 0) {
        m_themeCombo->setCurrentIndex(themeIndex);
    }

    m_openLastProjectCheck->setChecked(m_settings.openLastProjectOnStartup);

    QString docsRoot = QDir::homePath();
    if (!m_settings.projectRootFolders.isEmpty()) {
        docsRoot = m_settings.projectRootFolders.front();
    }
    m_newProjectLocationEdit->setText(docsRoot);
    m_newProjectNameEdit->setText(QStringLiteral("MyHephaestusProject"));
    UpdateNewProjectPathPreview();

    m_projectDelegate.SetPlaceholderImagePath(ResolvePlaceholderThumbnail());

    m_isInitializingUi = false;
}

void LauncherWindow::SaveSettings() const
{
    LauncherSettings settings = m_settings;
    settings.editorPath = m_editorPathEdit->text().trimmed();
    settings.runtimePath = m_runtimePathEdit->text().trimmed();
    settings.theme = m_themeCombo->currentText();
    settings.openLastProjectOnStartup = m_openLastProjectCheck->isChecked();

    settings.projectRootFolders.clear();
    for (int i = 0; i < m_projectRootsList->count(); ++i) {
        settings.projectRootFolders.push_back(m_projectRootsList->item(i)->text());
    }

    m_projectManager.SaveSettings(settings);
}

void LauncherWindow::ApplyTheme()
{
#if !HEPHAESTUS_USE_SHARED_ASSETS
    ApplyFallbackTheme();
    return;
#else
    LoadSharedFonts();

    const QStringList themeCandidates = {
        ResolveAsset(QStringLiteral("styles/hephaestus_launcher_dark.qss")),
        ResolveAsset(QStringLiteral("styles/editor_dark.qss")),
        ResolveAsset(QStringLiteral("styles/hephaestus_dark.qss")),
        ResolveAsset(QStringLiteral("styles/dark.qss"))
    };

    const QString themePath = PickFirstExisting(themeCandidates);
    if (!themePath.isEmpty()) {
        QFile file(themePath);
        if (file.open(QIODevice::ReadOnly)) {
            qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
            file.close();
            return;
        }
    }

    ApplyFallbackTheme();
#endif
}

void LauncherWindow::ApplyFallbackTheme()
{
#if !HEPHAESTUS_USE_SHARED_ASSETS
    QFile qssFile(QStringLiteral(":/fallback/minimal_dark.qss"));
    if (qssFile.open(QIODevice::ReadOnly)) {
        qApp->setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
        return;
    }
#endif

    qApp->setStyleSheet(BuiltinMinimalDarkQss());
}

void LauncherWindow::LoadSharedFonts()
{
#if HEPHAESTUS_USE_SHARED_ASSETS
    if (m_assets.fontsDir.isEmpty()) {
        return;
    }

    QDirIterator it(m_assets.fontsDir,
        QStringList{QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFontDatabase::addApplicationFont(it.next());
    }
#endif
}

void LauncherWindow::NavigateToSidebarItem(int row)
{
    if (row < 0 || row >= m_mainContent->count()) {
        return;
    }
    if (row == TemplatesPage) {
        return;
    }
    if (row == SettingsPage) {
        OpenSettingsWindow();
        if (m_sidebar->currentRow() == SettingsPage) {
            m_sidebar->setCurrentRow(RecentProjectsPage);
        }
        return;
    }
    m_mainContent->setCurrentIndex(row);
}

void LauncherWindow::OpenSettingsWindow()
{
    if (!m_settingsWindow) {
        m_settingsWindow = new Hephaestus::UI::SettingsWindow(this);
        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            m_settingsWindow = nullptr;
        });
    }

    m_settingsWindow->ConfigureContext(Hephaestus::EngineContext::Launcher, false, QString());
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

QModelIndex LauncherWindow::CurrentProjectIndex() const
{
    if (!m_projectGrid || !m_projectGrid->selectionModel()) {
        return {};
    }
    const QModelIndexList selected = m_projectGrid->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        return {};
    }
    return selected.front();
}

LauncherProject LauncherWindow::CurrentProject() const
{
    return m_projectModel.ProjectAt(CurrentProjectIndex());
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
    m_projectModel.SetProjects(projects);
    m_projectManager.SaveProjectIndex(projects);
    UpdateRecentEmptyState();
    UpdateProjectDetailsPanel();

    m_scanStatusLabel->setText(QStringLiteral("%1 projects found").arg(projects.size()));

    if (!m_settings.lastOpenedProject.isEmpty()) {
        const int row = m_projectModel.RowForProjectPath(m_settings.lastOpenedProject);
        if (row >= 0) {
            const QModelIndex idx = m_projectModel.index(row, 0);
            m_projectGrid->setCurrentIndex(idx);
        }
    }
}

void LauncherWindow::OnProjectSelectionChanged()
{
    UpdateProjectDetailsPanel();
}

void LauncherWindow::UpdateRecentEmptyState()
{
    const bool empty = (m_projectModel.rowCount() == 0);
    m_projectGrid->setVisible(!empty);
    m_emptyStateWidget->setVisible(empty);
}

void LauncherWindow::UpdateProjectDetailsPanel()
{
    const QModelIndex idx = CurrentProjectIndex();
    const bool hasSelection = idx.isValid();

    if (!hasSelection) {
        m_selectedNameLabel->setText(QStringLiteral("No project selected"));
        m_selectedDescriptionLabel->clear();
        m_selectedCreatedLabel->clear();
        m_selectedModifiedLabel->clear();
        m_selectedPathLabel->clear();
        m_openButton->setEnabled(false);
        m_openBottomButton->setEnabled(false);
        m_showExplorerButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
        return;
    }

    const LauncherProject project = m_projectModel.ProjectAt(idx);
    m_selectedNameLabel->setText(project.name);
    m_selectedDescriptionLabel->setText(project.description.isEmpty() ? QStringLiteral("No description") : project.description);
    const QLocale locale;
    m_selectedCreatedLabel->setText(QStringLiteral("Created: %1").arg(project.created.isValid() ? locale.toString(project.created, QLocale::ShortFormat) : QStringLiteral("Unknown")));
    m_selectedModifiedLabel->setText(QStringLiteral("Last Modified: %1").arg(project.lastModified.isValid() ? locale.toString(project.lastModified, QLocale::ShortFormat) : QStringLiteral("Unknown")));
    m_selectedPathLabel->setText(QStringLiteral("Path: %1").arg(project.projectDir));

    const bool canOpen = !project.broken;
    m_openButton->setEnabled(canOpen);
    m_openBottomButton->setEnabled(canOpen);
    m_showExplorerButton->setEnabled(true);
    m_deleteButton->setEnabled(true);
}

void LauncherWindow::UpdateNewProjectPathPreview()
{
    const QString name = m_newProjectNameEdit->text().trimmed();
    const QString location = m_newProjectLocationEdit->text().trimmed();
    if (name.isEmpty() || location.isEmpty()) {
        m_projectPathPreviewLabel->setText(QStringLiteral("<invalid>"));
        return;
    }
    m_projectPathPreviewLabel->setText(QDir(location).filePath(name));
}

void LauncherWindow::OpenProject(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    const LauncherProject project = m_projectModel.ProjectAt(index);
    if (project.broken) {
        QMessageBox::warning(this,
            QStringLiteral("Broken project"),
            QStringLiteral("This project has invalid metadata and cannot be opened until fixed."));
        return;
    }

    QString editorPath = EnsureExecutableExistsOrPrompt(m_editorPathEdit->text().trimmed(), QStringLiteral("Select Editor Executable"));
    if (editorPath.isEmpty()) {
        return;
    }

    m_editorPathEdit->setText(editorPath);
    m_settings.lastOpenedProject = project.projectDir;
    SaveSettings();

    QString error;
    if (!m_projectManager.OpenProjectInEditor(project, editorPath, &error)) {
        QMessageBox::critical(this, QStringLiteral("Open failed"), error);
        return;
    }

    close();
}

void LauncherWindow::SetProjectFavorite(const QModelIndex& index, bool favorite)
{
    if (!index.isValid()) {
        return;
    }

    LauncherProject project = m_projectModel.ProjectAt(index);
    QString error;
    if (!m_projectManager.SetFavorite(project, favorite, &error)) {
        QMessageBox::warning(this, QStringLiteral("Favorite update failed"), error);
        return;
    }

    m_projectModel.SetFavorite(index, favorite);
}

void LauncherWindow::DeleteSelectedProject()
{
    const QModelIndex idx = CurrentProjectIndex();
    if (!idx.isValid()) {
        return;
    }

    const LauncherProject project = m_projectModel.ProjectAt(idx);
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        QStringLiteral("Delete project"),
        QStringLiteral("Delete project '%1' and all files on disk?\n\nThis cannot be undone.").arg(project.name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_projectManager.DeleteProject(project, &error)) {
        QMessageBox::critical(this, QStringLiteral("Delete failed"), error);
        return;
    }

    RefreshProjectsAsync();
}

void LauncherWindow::ShowProjectInExplorer()
{
    const LauncherProject project = CurrentProject();
    if (project.projectDir.isEmpty()) {
        return;
    }

    if (!m_projectManager.OpenInExplorer(project.projectDir)) {
        QMessageBox::warning(this, QStringLiteral("Explorer"), QStringLiteral("Failed to open project folder."));
    }
}

void LauncherWindow::CreateNewProject()
{
    LauncherProject created;
    QString error;
    if (!m_projectManager.CreateProject(
            m_newProjectNameEdit->text().trimmed(),
            m_newProjectLocationEdit->text().trimmed(),
            m_engineVersionCombo->currentText(),
            m_createDefaultSceneCheck->isChecked(),
            &created,
            &error)) {
        QMessageBox::warning(this, QStringLiteral("Create project"), error);
        return;
    }

    if (!m_settings.projectRootFolders.contains(m_newProjectLocationEdit->text().trimmed())) {
        m_settings.projectRootFolders.push_back(m_newProjectLocationEdit->text().trimmed());
        m_projectRootsList->addItem(m_newProjectLocationEdit->text().trimmed());
        SaveSettings();
    }

    NavigateToSidebarItem(RecentProjectsPage);
    RefreshProjectsAsync();
}

QString LauncherWindow::EnsureExecutableExistsOrPrompt(const QString& currentPath, const QString& dialogTitle)
{
    const QFileInfo info(currentPath);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }

    QMessageBox::warning(this,
        QStringLiteral("Executable not found"),
        QStringLiteral("%1\n\nPlease choose the executable path.").arg(currentPath));

    const QString picked = QFileDialog::getOpenFileName(this,
        dialogTitle,
        Hephaestus::Tools::PathUtils::ExecutableDir(),
#if defined(Q_OS_WIN)
        QStringLiteral("Executable (*.exe)")
#else
        QStringLiteral("Executable (*)")
#endif
    );

    return picked;
}

QIcon LauncherWindow::LoadIcon(const QString& relativePath) const
{
#if !HEPHAESTUS_USE_SHARED_ASSETS
    const QString resourcePath = QStringLiteral(":/fallback/") + QFileInfo(relativePath).fileName();
    QIcon icon(resourcePath);
    if (!icon.isNull()) {
        return icon;
    }
    return style()->standardIcon(QStyle::SP_FileIcon);
#else
    QString resolved = ResolveAsset(relativePath);
    if (resolved.isEmpty()) {
        return style()->standardIcon(QStyle::SP_FileIcon);
    }

#if !HEPHAESTUS_QT_SVG_AVAILABLE
    if (resolved.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
        resolved.chop(4);
        resolved += QStringLiteral(".png");
    }
#endif

    QIcon icon(resolved);
    if (!icon.isNull()) {
        return icon;
    }

    return style()->standardIcon(QStyle::SP_FileIcon);
#endif
}

QString LauncherWindow::ResolveAsset(const QString& relativePath) const
{
#if !HEPHAESTUS_USE_SHARED_ASSETS
    Q_UNUSED(relativePath);
    return {};
#else
    if (m_assets.IsValid()) {
        const QString direct = m_assets.Resolve(relativePath);
        if (QFileInfo::exists(direct)) {
            return direct;
        }

        if (!m_assets.iconsDir.isEmpty() && relativePath.startsWith(QStringLiteral("icons/"), Qt::CaseInsensitive)) {
            const QString stripped = relativePath.mid(QStringLiteral("icons/").size());
            const QString candidate = QDir(m_assets.iconsDir).filePath(stripped);
            if (QFileInfo::exists(candidate)) {
                return candidate;
            }
        }

        if (!m_assets.imagesDir.isEmpty() && relativePath.startsWith(QStringLiteral("images/"), Qt::CaseInsensitive)) {
            const QString stripped = relativePath.mid(QStringLiteral("images/").size());
            const QString candidate = QDir(m_assets.imagesDir).filePath(stripped);
            if (QFileInfo::exists(candidate)) {
                return candidate;
            }
        }

        if (!m_assets.stylesDir.isEmpty() && relativePath.startsWith(QStringLiteral("styles/"), Qt::CaseInsensitive)) {
            const QString stripped = relativePath.mid(QStringLiteral("styles/").size());
            const QString candidate = QDir(m_assets.stylesDir).filePath(stripped);
            if (QFileInfo::exists(candidate)) {
                return candidate;
            }
        }
    }

    return {};
#endif
}

QString LauncherWindow::ResolvePlaceholderThumbnail() const
{
    const QStringList candidates = {
        ResolveAsset(QStringLiteral("icons/logo/256px.png")),
        ResolveAsset(QStringLiteral("icons/logo/128px.png")),
        ResolveAsset(QStringLiteral("images/placeholder_project.png"))
    };
    return PickFirstExisting(candidates);
}

void LauncherWindow::OnBrowseNewProjectLocation()
{
    const QString selected = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select Project Location"),
        m_newProjectLocationEdit->text().trimmed()
    );

    if (!selected.isEmpty()) {
        m_newProjectLocationEdit->setText(selected);
        UpdateNewProjectPathPreview();
    }
}

void LauncherWindow::OnBrowseEditorPath()
{
    const QString resolved = EnsureExecutableExistsOrPrompt(m_editorPathEdit->text().trimmed(),
        QStringLiteral("Select Editor Executable"));
    if (resolved.isEmpty()) {
        return;
    }

    m_editorPathEdit->setText(resolved);
    SaveSettings();
}

void LauncherWindow::OnBrowseRuntimePath()
{
    const QString resolved = EnsureExecutableExistsOrPrompt(m_runtimePathEdit->text().trimmed(),
        QStringLiteral("Select Runtime Executable"));
    if (resolved.isEmpty()) {
        return;
    }

    m_runtimePathEdit->setText(resolved);
    SaveSettings();
}

void LauncherWindow::OnAddProjectRoot()
{
    const QString selected = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Add Project Root Folder"),
        QDir::homePath());
    if (selected.isEmpty()) {
        return;
    }

    for (int i = 0; i < m_projectRootsList->count(); ++i) {
        if (QDir::cleanPath(m_projectRootsList->item(i)->text()) == QDir::cleanPath(selected)) {
            return;
        }
    }

    m_projectRootsList->addItem(selected);
    SaveSettings();
    m_settings = m_projectManager.LoadSettings();
    RefreshProjectsAsync();
}

void LauncherWindow::OnRemoveProjectRoot()
{
    auto* item = m_projectRootsList->currentItem();
    if (!item) {
        return;
    }

    delete item;
    SaveSettings();
    m_settings = m_projectManager.LoadSettings();
    RefreshProjectsAsync();
}

} // namespace Hephaestus::Launcher
