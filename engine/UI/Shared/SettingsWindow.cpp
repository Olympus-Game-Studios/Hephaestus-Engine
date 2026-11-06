#include "SettingsWindow.h"
#include "InputMapEditorWidget.h"
#include "PhysicsLayerEditorWidget.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QFileInfo>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QDir>
#include <QDirIterator>
#include <QtGlobal>

#include <algorithm>
#include <filesystem>

namespace {

bool VariantEquivalent(const QVariant& lhs, const QVariant& rhs)
{
    if (lhs.typeId() == QMetaType::Double || rhs.typeId() == QMetaType::Double ||
        lhs.typeId() == QMetaType::Float || rhs.typeId() == QMetaType::Float) {
        return qFuzzyCompare(lhs.toDouble() + 1.0, rhs.toDouble() + 1.0);
    }
    return lhs == rhs;
}

} // namespace

namespace Hephaestus::UI {

SettingsWindow::SettingsWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Settings"));
    setModal(false);
    resize(980, 680);

    BuildUi();
    LoadWindowState();

    ConfigureContext(Hephaestus::EngineContext::Launcher, false);
}

void SettingsWindow::ConfigureContext(Hephaestus::EngineContext context,
                                      bool hasOpenProject,
                                      const QString& projectRoot)
{
    m_context = context;
    m_hasOpenProject = hasOpenProject;
    m_projectRoot = projectRoot;

    m_engineManager.Load();
    m_enginePending = m_engineManager.Values();

    if (m_hasOpenProject && !m_projectRoot.trimmed().isEmpty()) {
        if (!m_projectManager) {
            m_projectManager = std::make_unique<Hephaestus::Tools::ProjectSettingsManager>(m_projectRoot);
        } else {
            m_projectManager->SetProjectRoot(m_projectRoot);
        }
        m_projectManager->Load();
        m_projectPending = m_projectManager->Values();
    } else {
        m_projectManager.reset();
        m_projectPending.clear();
    }

    RebuildTabs();
    UpdateDirtyState();
}

void SettingsWindow::closeEvent(QCloseEvent* event)
{
    if (!TryCloseWithUnsavedPrompt()) {
        event->ignore();
        return;
    }

    SaveWindowState();
    QDialog::closeEvent(event);
}

void SettingsWindow::reject()
{
    if (!TryCloseWithUnsavedPrompt()) {
        return;
    }
    SaveWindowState();
    QDialog::reject();
}

void SettingsWindow::OnApply()
{
    ApplyChanges();
}

void SettingsWindow::BuildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->tabBar()->setUsesScrollButtons(false);
    m_tabs->tabBar()->setStyleSheet(QStringLiteral("QTabBar::tab { margin-right: 6px; }"));
    root->addWidget(m_tabs, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_applyButton = buttons->addButton(QStringLiteral("Apply"), QDialogButtonBox::ApplyRole);
    m_cancelButton = buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);

    m_applyButton->setAutoDefault(false);
    m_applyButton->setDefault(false);
    m_cancelButton->setAutoDefault(false);
    m_cancelButton->setDefault(false);
    m_applyButton->setMinimumWidth(96);
    m_cancelButton->setMinimumWidth(96);

    if (buttons->layout()) {
        buttons->layout()->setSpacing(10);
    }

    connect(m_applyButton, &QPushButton::clicked, this, &SettingsWindow::OnApply);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsWindow::reject);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        m_lastTabKey = m_tabs->tabBar()->tabData(index).toString();
    });

    root->addWidget(buttons);
}

void SettingsWindow::RebuildTabs()
{
    m_tabs->clear();
    m_inputMapEditor = nullptr;
    m_physicsLayerEditor = nullptr;
    m_inputProfileEdit = nullptr;
    m_engineTab = TabUi();
    m_projectTab = TabUi();

    BuildTab(m_engineTab,
             QStringLiteral("engine"),
             QStringLiteral("Engine Settings"),
             m_engineManager.Definitions(),
             m_enginePending);

    const bool showProjectTab =
        (m_context == Hephaestus::EngineContext::Editor) &&
        m_hasOpenProject &&
        (m_projectManager != nullptr);

    if (showProjectTab) {
        BuildTab(m_projectTab,
                 QStringLiteral("project"),
                 QStringLiteral("Project Settings"),
                 m_projectManager->Definitions(),
                 m_projectPending);
        AttachPhysicsLayerPage();
    }

    int targetIndex = 0;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabBar()->tabData(i).toString() == m_lastTabKey) {
            targetIndex = i;
            break;
        }
    }
    m_tabs->setCurrentIndex(targetIndex);
}

void SettingsWindow::BuildTab(TabUi& tab,
                              const QString& key,
                              const QString& title,
                              const QVector<Hephaestus::Tools::SettingDefinition>& definitions,
                              const QHash<QString, QVariant>& values)
{
    tab.key = key;
    tab.definitions = definitions;

    tab.root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(tab.root);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    tab.search = new QLineEdit(tab.root);
    tab.search->setPlaceholderText(QStringLiteral("Search settings..."));
    rootLayout->addWidget(tab.search);

    auto* content = new QHBoxLayout();
    content->setSpacing(8);

    auto* leftPane = new QWidget(tab.root);
    leftPane->setMinimumWidth(220);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto* categoryLabel = new QLabel(QStringLiteral("Categories"), leftPane);
    tab.categories = new QListWidget(leftPane);
    tab.resetCategoryButton = new QPushButton(QStringLiteral("Reset Category Defaults"), leftPane);

    leftLayout->addWidget(categoryLabel);
    leftLayout->addWidget(tab.categories, 1);
    leftLayout->addWidget(tab.resetCategoryButton);

    tab.categoryPages = new QStackedWidget(tab.root);

    content->addWidget(leftPane, 0);
    content->addWidget(tab.categoryPages, 1);
    rootLayout->addLayout(content, 1);

    QStringList categoryIds;
    for (const Hephaestus::Tools::SettingDefinition& def : definitions) {
        if (!categoryIds.contains(def.categoryId)) {
            categoryIds.push_back(def.categoryId);

            auto* item = new QListWidgetItem(def.categoryLabel, tab.categories);
            item->setData(Qt::UserRole, def.categoryId);
            tab.categoryItems.insert(def.categoryId, item);

            auto* categoryPage = new QWidget(tab.categoryPages);
            auto* pageRoot = new QVBoxLayout(categoryPage);
            pageRoot->setContentsMargins(4, 4, 4, 4);

            auto* scroll = new QScrollArea(categoryPage);
            scroll->setWidgetResizable(true);

            auto* scrollContent = new QWidget(scroll);
            auto* form = new QFormLayout(scrollContent);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            form->setVerticalSpacing(8);
            scrollContent->setLayout(form);

            scroll->setWidget(scrollContent);
            pageRoot->addWidget(scroll);

            tab.categoryPages->addWidget(categoryPage);
            tab.categoryPagesById.insert(def.categoryId, categoryPage);
        }
    }

    for (const Hephaestus::Tools::SettingDefinition& def : definitions) {
        QWidget* categoryPage = tab.categoryPagesById.value(def.categoryId);
        auto* scroll = categoryPage->findChild<QScrollArea*>();
        auto* form = qobject_cast<QFormLayout*>(scroll->widget()->layout());
        const QVariant value = values.value(def.id, def.defaultValue);

        QWidget* editor = BuildEditor(def, value, tab);
        QWidget* row = new QWidget(scroll->widget());
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(editor);

        form->addRow(def.label, row);
        tab.rowBySettingId.insert(def.id, row);
        tab.editorBySettingId.insert(def.id, editor);
    }

    connect(tab.categories, &QListWidget::currentRowChanged, this, [&tab](int row) {
        if (row < 0) {
            return;
        }
        QListWidgetItem* item = tab.categories->item(row);
        if (!item || item->isHidden()) {
            return;
        }
        const QString categoryId = item->data(Qt::UserRole).toString();
        QWidget* categoryPage = tab.categoryPagesById.value(categoryId);
        if (categoryPage) {
            tab.categoryPages->setCurrentWidget(categoryPage);
        }
        tab.resetCategoryButton->setEnabled(categoryId != QStringLiteral("physics.layers"));
    });

    connect(tab.search, &QLineEdit::textChanged, this, [this, &tab](const QString& text) {
        ApplySearchFilter(tab, text);
    });

    connect(tab.resetCategoryButton, &QPushButton::clicked, this, [this, &tab]() {
        const QListWidgetItem* item = tab.categories->currentItem();
        if (!item) {
            return;
        }

        const QString categoryId = item->data(Qt::UserRole).toString();
        QHash<QString, QVariant>& pending = PendingForTab(tab.key);

        for (const Hephaestus::Tools::SettingDefinition& def : tab.definitions) {
            if (def.categoryId == categoryId) {
                pending[def.id] = def.defaultValue;
            }
        }

        PopulateTabValues(tab, pending);
        UpdateDirtyState();
    });

    if (tab.categories->count() > 0) {
        tab.categories->setCurrentRow(0);
    }

    const int idx = m_tabs->addTab(tab.root, title);
    m_tabs->tabBar()->setTabData(idx, key);
}

QWidget* SettingsWindow::BuildEditor(const Hephaestus::Tools::SettingDefinition& definition,
                                     const QVariant& value,
                                     TabUi& tab)
{
    using Hephaestus::Tools::SettingControlType;

    if (definition.id == QStringLiteral("project.input.defaultInputProfile") &&
        tab.key == QStringLiteral("project")) {
        auto* container = new QWidget();
        auto* root = new QVBoxLayout(container);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(6);

        auto* pathRow = new QHBoxLayout();
        m_inputProfileEdit = new QLineEdit(value.toString(), container);
        m_inputProfileEdit->setObjectName(QStringLiteral("inputProfilePath"));
        auto* reload = new QPushButton(QStringLiteral("Reload"), container);
        pathRow->addWidget(m_inputProfileEdit, 1);
        pathRow->addWidget(reload);
        root->addLayout(pathRow);

        m_inputMapEditor = new InputMapEditorWidget(container);
        root->addWidget(m_inputMapEditor);
        QString loadError;
        m_inputMapEditor->Load(
            std::filesystem::path(m_projectRoot.toStdWString()),
            value.toString(), &loadError);

        connect(m_inputProfileEdit, &QLineEdit::textChanged, this,
                [this, key = tab.key, id = definition.id](const QString& text) {
            PendingForTab(key)[id] = text;
            UpdateDirtyState();
        });
        connect(reload, &QPushButton::clicked, this, [this]() {
            if (!m_inputMapEditor || !m_inputProfileEdit) return;
            QString error;
            if (!m_inputMapEditor->Load(
                    std::filesystem::path(m_projectRoot.toStdWString()),
                    m_inputProfileEdit->text(), &error)) {
                QMessageBox::warning(this, QStringLiteral("Input Map"),
                                     error.isEmpty() ? QStringLiteral("Could not load input map.") : error);
            }
            UpdateDirtyState();
        });
        m_inputMapEditor->SetChangedCallback([this]() { UpdateDirtyState(); });
        return container;
    }

    if (definition.controlType == SettingControlType::Bool) {
        auto* check = new QCheckBox();
        check->setChecked(value.toBool());
        connect(check, &QCheckBox::toggled, this, [this, key = tab.key, id = definition.id](bool checked) {
            PendingForTab(key)[id] = checked;
            UpdateDirtyState();
        });
        return check;
    }

    if (definition.controlType == SettingControlType::Int) {
        auto* spin = new QSpinBox();
        spin->setRange(definition.minValue.isValid() ? definition.minValue.toInt() : -999999,
                       definition.maxValue.isValid() ? definition.maxValue.toInt() : 999999);
        spin->setValue(value.toInt());
        connect(spin, &QSpinBox::valueChanged, this, [this, key = tab.key, id = definition.id](int number) {
            PendingForTab(key)[id] = number;
            UpdateDirtyState();
        });
        return spin;
    }

    if (definition.controlType == SettingControlType::Float) {
        auto* spin = new QDoubleSpinBox();
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setRange(definition.minValue.isValid() ? definition.minValue.toDouble() : -999999.0,
                       definition.maxValue.isValid() ? definition.maxValue.toDouble() : 999999.0);
        spin->setValue(value.toDouble());
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key = tab.key, id = definition.id](double number) {
            PendingForTab(key)[id] = number;
            UpdateDirtyState();
        });
        return spin;
    }

    if (definition.controlType == SettingControlType::Enum) {
        auto* combo = new QComboBox();
        combo->addItems(definition.enumOptions);
        const int index = combo->findText(value.toString());
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
        connect(combo, &QComboBox::currentTextChanged, this, [this, key = tab.key, id = definition.id](const QString& text) {
            PendingForTab(key)[id] = text;
            UpdateDirtyState();
        });
        return combo;
    }

    if (definition.controlType == SettingControlType::Path) {
        auto* row = new QWidget();
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* edit = new QLineEdit(row);
        edit->setText(value.toString());
        auto* browse = new QPushButton(QStringLiteral("..."), row);
        browse->setFixedWidth(32);

        layout->addWidget(edit, 1);
        layout->addWidget(browse);

        connect(edit, &QLineEdit::textChanged, this, [this, key = tab.key, id = definition.id](const QString& text) {
            PendingForTab(key)[id] = text;
            UpdateDirtyState();
        });

        connect(browse, &QPushButton::clicked, this, [this, edit, key = tab.key, id = definition.id]() {
            QString selected;
            if (id == QStringLiteral("project.rendering.defaultSkybox")) {
                selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select Skybox"), m_projectRoot,
                                                        QStringLiteral("HDR images (*.hdr *.exr)"));
                const QString relative = QDir(m_projectRoot).relativeFilePath(selected);
                if (!selected.isEmpty() && !relative.startsWith(QStringLiteral(".."))) {
                    selected = relative;
                }
            } else {
                selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Path"), edit->text());
            }
            if (!selected.isEmpty()) {
                edit->setText(selected);
                PendingForTab(key)[id] = selected;
                UpdateDirtyState();
            }
        });

        return row;
    }

    if (definition.id == QStringLiteral("project.info.defaultScene") &&
        tab.key == QStringLiteral("project")) {
        auto* row = new QWidget();
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* combo = new QComboBox(row);
        combo->setObjectName(QStringLiteral("defaultSceneCombo"));
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        auto* refresh = new QPushButton(QStringLiteral("Refresh"), row);
        refresh->setObjectName(QStringLiteral("defaultSceneRefreshButton"));

        layout->addWidget(combo, 1);
        layout->addWidget(refresh);

        const auto updatePending = [this, key = tab.key, id = definition.id, combo]() {
            PendingForTab(key)[id] = combo->currentData().toString();
            UpdateDirtyState();
        };

        RefreshDefaultSceneEditor(row, value.toString(), tab.key);

        connect(combo, &QComboBox::currentIndexChanged, this, [updatePending](int) {
            updatePending();
        });

        connect(refresh, &QPushButton::clicked, this, [this, row, key = tab.key, id = definition.id]() {
            const QString selected = PendingForTab(key).value(id).toString();
            RefreshDefaultSceneEditor(row, selected, key);
        });

        return row;
    }

    auto* edit = new QLineEdit();
    edit->setText(value.toString());
    connect(edit, &QLineEdit::textChanged, this, [this, key = tab.key, id = definition.id](const QString& text) {
        PendingForTab(key)[id] = text;
        UpdateDirtyState();
    });
    return edit;
}

void SettingsWindow::PopulateTabValues(TabUi& tab, const QHash<QString, QVariant>& values)
{
    using Hephaestus::Tools::SettingControlType;

    for (const Hephaestus::Tools::SettingDefinition& def : tab.definitions) {
        QWidget* editor = tab.editorBySettingId.value(def.id, nullptr);
        if (!editor) {
            continue;
        }

        const QVariant value = values.value(def.id, def.defaultValue);

        switch (def.controlType) {
        case SettingControlType::Bool: {
            if (auto* check = qobject_cast<QCheckBox*>(editor)) {
                check->setChecked(value.toBool());
            }
            break;
        }
        case SettingControlType::Int: {
            if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
                spin->setValue(value.toInt());
            }
            break;
        }
        case SettingControlType::Float: {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
                spin->setValue(value.toDouble());
            }
            break;
        }
        case SettingControlType::Enum: {
            if (auto* combo = qobject_cast<QComboBox*>(editor)) {
                const int idx = combo->findText(value.toString());
                if (idx >= 0) {
                    combo->setCurrentIndex(idx);
                }
            }
            break;
        }
        case SettingControlType::Path: {
            if (auto* line = editor->findChild<QLineEdit*>()) {
                line->setText(value.toString());
            }
            break;
        }
        case SettingControlType::String:
        default: {
            if (def.id == QStringLiteral("project.input.defaultInputProfile")) {
                if (auto* line = editor->findChild<QLineEdit*>(QStringLiteral("inputProfilePath"))) {
                    line->setText(value.toString());
                }
                if (m_inputMapEditor) {
                    QString error;
                    m_inputMapEditor->Load(
                        std::filesystem::path(m_projectRoot.toStdWString()),
                        value.toString(), &error);
                }
                break;
            }
            if (auto* combo = editor->findChild<QComboBox*>(QStringLiteral("defaultSceneCombo"))) {
                RefreshDefaultSceneEditor(editor, value.toString(), tab.key);
                const QString desired = value.toString().trimmed().replace('\\', '/');
                for (int i = 0; i < combo->count(); ++i) {
                    if (combo->itemData(i).toString().trimmed().replace('\\', '/') == desired) {
                        combo->setCurrentIndex(i);
                        break;
                    }
                }
                break;
            }
            if (auto* line = qobject_cast<QLineEdit*>(editor)) {
                line->setText(value.toString());
            }
            break;
        }
        }
    }
}

QStringList SettingsWindow::CollectProjectScenePaths() const
{
    QStringList scenes;
    if (m_projectRoot.trimmed().isEmpty()) {
        return scenes;
    }

    const QDir projectDir(m_projectRoot);
    const QString scenesRoot = projectDir.filePath(QStringLiteral("assets/scenes"));
    if (!QDir(scenesRoot).exists()) {
        return scenes;
    }

    QDirIterator it(scenesRoot,
                    QStringList() << QStringLiteral("*.hpscene") << QStringLiteral("*.hescene"),
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString absolutePath = it.next();
        QString relativePath = projectDir.relativeFilePath(absolutePath);
        relativePath = QDir::fromNativeSeparators(relativePath);
        scenes.push_back(relativePath);
    }

    std::sort(scenes.begin(), scenes.end(), [](const QString& lhs, const QString& rhs) {
        return lhs.compare(rhs, Qt::CaseInsensitive) < 0;
    });

    return scenes;
}

void SettingsWindow::RefreshDefaultSceneEditor(QWidget* editor,
                                               const QString& selectedValue,
                                               const QString& tabKey)
{
    QComboBox* combo = editor ? editor->findChild<QComboBox*>(QStringLiteral("defaultSceneCombo")) : nullptr;
    if (!combo) {
        return;
    }

    const QString normalizedSelected = QDir::fromNativeSeparators(selectedValue.trimmed());
    const QStringList scenes = CollectProjectScenePaths();

    QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(QStringLiteral("(None)"), QString());

    int selectedIndex = 0;
    for (const QString& scene : scenes) {
        combo->addItem(scene, scene);
        if (!normalizedSelected.isEmpty() && scene.compare(normalizedSelected, Qt::CaseInsensitive) == 0) {
            selectedIndex = combo->count() - 1;
        }
    }

    if (!normalizedSelected.isEmpty() && selectedIndex == 0) {
        QFileInfo selectedInfo(normalizedSelected);
        const QString label = QStringLiteral("%1 (Missing)").arg(selectedInfo.fileName().isEmpty() ? normalizedSelected : selectedInfo.fileName());
        combo->addItem(label, normalizedSelected);
        selectedIndex = combo->count() - 1;
    }

    combo->setCurrentIndex(selectedIndex);

    PendingForTab(tabKey)[QStringLiteral("project.info.defaultScene")] = combo->currentData().toString();
}

void SettingsWindow::ApplySearchFilter(TabUi& tab, const QString& searchText)
{
    const QString needle = searchText.trimmed();

    QHash<QString, bool> categoryVisible;
    for (const Hephaestus::Tools::SettingDefinition& def : tab.definitions) {
        const bool match = needle.isEmpty() ||
            def.label.contains(needle, Qt::CaseInsensitive) ||
            def.categoryLabel.contains(needle, Qt::CaseInsensitive) ||
            def.id.contains(needle, Qt::CaseInsensitive);

        if (QWidget* row = tab.rowBySettingId.value(def.id, nullptr)) {
            row->setVisible(match);
        }
        categoryVisible[def.categoryId] = categoryVisible.value(def.categoryId, false) || match;
    }

    QListWidgetItem* firstVisible = nullptr;
    for (auto it = tab.categoryItems.begin(); it != tab.categoryItems.end(); ++it) {
        const bool visible = categoryVisible.value(it.key(), false);
        it.value()->setHidden(!visible);
        if (visible && !firstVisible) {
            firstVisible = it.value();
        }
    }

    if (QListWidgetItem* physics = tab.categoryItems.value(QStringLiteral("physics.layers"))) {
        const bool visible = needle.isEmpty()
            || QStringLiteral("physics layers collision matrix").contains(needle, Qt::CaseInsensitive);
        physics->setHidden(!visible);
        if (visible && !firstVisible) {
            firstVisible = physics;
        }
    }

    QListWidgetItem* current = tab.categories->currentItem();
    if (!current || current->isHidden()) {
        if (firstVisible) {
            tab.categories->setCurrentItem(firstVisible);
        }
    }
}

void SettingsWindow::UpdateDirtyState()
{
    m_engineDirty = false;
    const QHash<QString, QVariant> engineCurrent = m_engineManager.Values();
    for (const Hephaestus::Tools::SettingDefinition& def : m_engineTab.definitions) {
        if (!VariantEquivalent(m_enginePending.value(def.id), engineCurrent.value(def.id))) {
            m_engineDirty = true;
            break;
        }
    }

    m_projectDirty = false;
    if (m_projectManager) {
        const QHash<QString, QVariant> projectCurrent = m_projectManager->Values();
        for (const Hephaestus::Tools::SettingDefinition& def : m_projectTab.definitions) {
            if (!VariantEquivalent(m_projectPending.value(def.id), projectCurrent.value(def.id))) {
                m_projectDirty = true;
                break;
            }
        }
        if (m_inputMapEditor && m_inputMapEditor->IsDirty()) {
            m_projectDirty = true;
        }
        if (m_physicsLayerEditor && m_physicsLayerEditor->IsDirty()) {
            m_projectDirty = true;
        }
    }

    m_applyButton->setEnabled(m_engineDirty || m_projectDirty);
}

bool SettingsWindow::TryCloseWithUnsavedPrompt()
{
    if (!m_engineDirty && !m_projectDirty) {
        return true;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved Settings"),
        QStringLiteral("You have unsaved settings changes. Save before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (result == QMessageBox::Cancel) {
        return false;
    }

    if (result == QMessageBox::Save) {
        return ApplyChanges();
    }

    return true;
}

bool SettingsWindow::ApplyChanges()
{
    const bool hadChanges = m_engineDirty || m_projectDirty;
    if (m_engineDirty) {
        m_engineManager.SetValues(m_enginePending);
        if (!m_engineManager.Save()) {
            QMessageBox::critical(this,
                                  QStringLiteral("Save Failed"),
                                  QStringLiteral("Failed to save engine settings."));
            return false;
        }
        m_engineManager.Load();
        m_enginePending = m_engineManager.Values();
    }

    if (m_projectDirty && m_projectManager) {
        if (m_inputMapEditor && m_inputMapEditor->IsDirty()) {
            QString error;
            const QString profile = m_inputProfileEdit
                ? m_inputProfileEdit->text()
                : m_projectPending.value(
                      QStringLiteral("project.input.defaultInputProfile"),
                      QStringLiteral("assets/input/Default.hpinput")).toString();
            if (!m_inputMapEditor->Save(
                    std::filesystem::path(m_projectRoot.toStdWString()),
                    profile, &error)) {
                QMessageBox::critical(
                    this, QStringLiteral("Save Failed"),
                    error.isEmpty() ? QStringLiteral("Failed to save the input map.") : error);
                return false;
            }
        }
        if (m_physicsLayerEditor && m_physicsLayerEditor->IsDirty()) {
            QString error;
            if (!m_physicsLayerEditor->Save(&error)) {
                QMessageBox::critical(
                    this, QStringLiteral("Save Failed"),
                    error.isEmpty() ? QStringLiteral("Failed to save physics layers.") : error);
                return false;
            }
        }
        m_projectManager->SetValues(m_projectPending);
        if (!m_projectManager->Save()) {
            QMessageBox::critical(this,
                                  QStringLiteral("Save Failed"),
                                  QStringLiteral("Failed to save project settings."));
            return false;
        }
        m_projectManager->Load();
        m_projectPending = m_projectManager->Values();
    }

    UpdateDirtyState();
    if (hadChanges) {
        emit settingsApplied();
    }
    return true;
}

QHash<QString, QVariant>& SettingsWindow::PendingForTab(const QString& tabKey)
{
    if (tabKey == QStringLiteral("project")) {
        return m_projectPending;
    }
    return m_enginePending;
}

QHash<QString, QVariant> SettingsWindow::CurrentValuesForTab(const QString& tabKey) const
{
    if (tabKey == QStringLiteral("project") && m_projectManager) {
        return m_projectManager->Values();
    }
    return m_engineManager.Values();
}

void SettingsWindow::AttachPhysicsLayerPage()
{
    auto* item = new QListWidgetItem(QStringLiteral("Physics"), m_projectTab.categories);
    item->setData(Qt::UserRole, QStringLiteral("physics.layers"));
    m_projectTab.categoryItems.insert(QStringLiteral("physics.layers"), item);

    auto* page = new QWidget(m_projectTab.categoryPages);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    m_physicsLayerEditor = new PhysicsLayerEditorWidget(page);
    layout->addWidget(m_physicsLayerEditor);
    QString error;
    if (!m_physicsLayerEditor->Load(std::filesystem::path(m_projectRoot.toStdWString()), &error)) {
        QMessageBox::warning(this, QStringLiteral("Physics Layers"),
                             error.isEmpty() ? QStringLiteral("Could not load physics layers.") : error);
    }
    m_physicsLayerEditor->SetChangedCallback([this]() { UpdateDirtyState(); });
    m_projectTab.categoryPages->addWidget(page);
    m_projectTab.categoryPagesById.insert(QStringLiteral("physics.layers"), page);
}

void SettingsWindow::SaveWindowState() const
{
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("SharedUi"));
    settings.setValue(QStringLiteral("settingsWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("settingsWindow/lastTab"), m_lastTabKey);
}

void SettingsWindow::LoadWindowState()
{
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("SharedUi"));
    const QByteArray geometry = settings.value(QStringLiteral("settingsWindow/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QString tabKey = settings.value(QStringLiteral("settingsWindow/lastTab"), QStringLiteral("engine")).toString();
    if (!tabKey.isEmpty()) {
        m_lastTabKey = tabKey;
    }
}

} // namespace Hephaestus::UI
