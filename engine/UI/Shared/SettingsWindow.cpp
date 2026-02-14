#include "SettingsWindow.h"

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
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QtGlobal>

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
            const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Path"), edit->text());
            if (!selected.isEmpty()) {
                edit->setText(selected);
                PendingForTab(key)[id] = selected;
                UpdateDirtyState();
            }
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
            if (auto* line = qobject_cast<QLineEdit*>(editor)) {
                line->setText(value.toString());
            }
            break;
        }
        }
    }
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
