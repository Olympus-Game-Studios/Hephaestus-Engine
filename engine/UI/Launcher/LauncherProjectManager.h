#pragma once

#include "LauncherProject.h"

#include <QObject>
#include <QStringList>
#include <QVector>

namespace Hephaestus::Launcher {

struct LauncherSettings {
    QString editorPath;
    QString runtimePath;
    QStringList projectRootFolders;
    QString theme = QStringLiteral("Dark");
    bool openLastProjectOnStartup = false;
    QString lastOpenedProject;
    QString newProjectMode = QStringLiteral("default");
};

class LauncherProjectManager : public QObject {
    Q_OBJECT

public:
    explicit LauncherProjectManager(QObject* parent = nullptr);

    LauncherSettings LoadSettings() const;
    void SaveSettings(const LauncherSettings& settings) const;
    QVector<LauncherProject> LoadProjectIndex() const;
    void SaveProjectIndex(const QVector<LauncherProject>& projects) const;

    QString DetectEditorPath() const;
    QString DetectRuntimePath() const;

    bool ReadProject(const QString& projectFilePath, LauncherProject* outProject) const;
    bool CreateProject(const QString& projectName,
                       const QString& locationFolder,
                       const QString& engineVersion,
                       bool createDefaultScene,
                       const QString& editorMode,
                       LauncherProject* outProject,
                       QString* outError);
    bool DeleteProject(const LauncherProject& project, QString* outError);
    bool OpenProjectInEditor(const LauncherProject& project, const QString& editorPath, QString* outError) const;
    bool OpenInExplorer(const QString& path) const;

private:
    QString ProjectFilePathForNewProject(const QString& projectDir) const;
};

} // namespace Hephaestus::Launcher
