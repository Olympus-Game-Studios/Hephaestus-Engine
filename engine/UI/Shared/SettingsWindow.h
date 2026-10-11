#pragma once

#include "../../ToolsCommon/EngineContext.h"
#include "../../ToolsCommon/EngineSettingsManager.h"
#include "../../ToolsCommon/ProjectSettingsManager.h"

#include <QDialog>
#include <QHash>

#include <memory>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QWidget;

namespace Hephaestus::UI {

class InputMapEditorWidget;
class PhysicsLayerEditorWidget;

class SettingsWindow : public QDialog {
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget* parent = nullptr);

    void ConfigureContext(Hephaestus::EngineContext context,
                          bool hasOpenProject,
                          const QString& projectRoot = QString());

signals:
    void settingsApplied();

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void OnApply();

private:
    struct TabUi {
        QString key;
        QWidget* root = nullptr;
        QLineEdit* search = nullptr;
        QListWidget* categories = nullptr;
        QStackedWidget* categoryPages = nullptr;
        QPushButton* resetCategoryButton = nullptr;
        QHash<QString, QListWidgetItem*> categoryItems;
        QHash<QString, QWidget*> categoryPagesById;
        QHash<QString, QWidget*> rowBySettingId;
        QHash<QString, QWidget*> editorBySettingId;
        QVector<Hephaestus::Tools::SettingDefinition> definitions;
    };

    void BuildUi();
    void RebuildTabs();
    void BuildTab(TabUi& tab,
                  const QString& key,
                  const QString& title,
                  const QVector<Hephaestus::Tools::SettingDefinition>& definitions,
                  const QHash<QString, QVariant>& values);
    QWidget* BuildEditor(const Hephaestus::Tools::SettingDefinition& definition,
                         const QVariant& value,
                         TabUi& tab);

    void PopulateTabValues(TabUi& tab, const QHash<QString, QVariant>& values);
    void ApplySearchFilter(TabUi& tab, const QString& searchText);
    void UpdateDirtyState();
    bool TryCloseWithUnsavedPrompt();
    bool ApplyChanges();

    QHash<QString, QVariant>& PendingForTab(const QString& tabKey);
    QHash<QString, QVariant> CurrentValuesForTab(const QString& tabKey) const;

    QStringList CollectProjectScenePaths() const;
    void RefreshDefaultSceneEditor(QWidget* editor, const QString& selectedValue, const QString& tabKey);

    void SaveWindowState() const;
    void LoadWindowState();

    Hephaestus::EngineContext m_context = Hephaestus::EngineContext::Launcher;
    bool m_hasOpenProject = false;
    QString m_projectRoot;
    QString m_lastTabKey = QStringLiteral("engine");

    Hephaestus::Tools::EngineSettingsManager m_engineManager;
    std::unique_ptr<Hephaestus::Tools::ProjectSettingsManager> m_projectManager;

    QHash<QString, QVariant> m_enginePending;
    QHash<QString, QVariant> m_projectPending;
    bool m_engineDirty = false;
    bool m_projectDirty = false;

    QTabWidget* m_tabs = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_cancelButton = nullptr;

    TabUi m_engineTab;
    TabUi m_projectTab;
    InputMapEditorWidget* m_inputMapEditor = nullptr;
    PhysicsLayerEditorWidget* m_physicsLayerEditor = nullptr;
    QLineEdit* m_inputProfileEdit = nullptr;

    void AttachPhysicsLayerPage();
};

} // namespace Hephaestus::UI
