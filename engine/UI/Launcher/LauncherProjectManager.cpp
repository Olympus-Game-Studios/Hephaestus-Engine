#include "LauncherProjectManager.h"

#include "../../ToolsCommon/JsonUtils.h"
#include "../../ToolsCommon/PathUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString FirstExistingFile(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QDateTime ReadEpochDate(const QJsonObject& obj, const QString& key, const QDateTime& fallback)
{
    const QJsonValue value = obj.value(key);
    if (!value.isDouble()) {
        return fallback;
    }

    const qint64 epoch = static_cast<qint64>(value.toDouble());
    return QDateTime::fromSecsSinceEpoch(epoch);
}

QJsonObject BuildProjectJson(const Hephaestus::Launcher::LauncherProject& project)
{
    QJsonObject root;
    root[QStringLiteral("formatVersion")] = 1;
    root[QStringLiteral("name")] = project.name;
    root[QStringLiteral("description")] = project.description;
    root[QStringLiteral("engineVersion")] = project.engineVersion;
    root[QStringLiteral("created")] = project.created.toSecsSinceEpoch();
    root[QStringLiteral("lastModified")] = project.lastModified.toSecsSinceEpoch();
    root[QStringLiteral("isFavorite")] = project.favorite;
    root[QStringLiteral("screenshot")] = project.screenshotPath;

    QJsonArray tags;
    for (const QString& tag : project.tags) {
        tags.append(tag);
    }
    root[QStringLiteral("tags")] = tags;

    return root;
}

bool IsProjectMetadataFile(const QString& projectDirPath, const QString& candidatePath)
{
    const QDir projectDir(projectDirPath);
    const QString rel = QDir::cleanPath(projectDir.relativeFilePath(candidatePath));
    return rel.compare(QStringLiteral("project.heproject"), Qt::CaseInsensitive) == 0 ||
           rel.compare(QStringLiteral(".hephaestus/project.heproject"), Qt::CaseInsensitive) == 0;
}

QDateTime ComputeProjectContentLastModified(const QString& projectDirPath, const QDateTime& fallback)
{
    QDateTime latest = fallback;

    QDirIterator it(projectDirPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        if (IsProjectMetadataFile(projectDirPath, filePath)) {
            continue;
        }

        const QFileInfo info(filePath);
        if (info.exists() && info.lastModified().isValid() && info.lastModified() > latest) {
            latest = info.lastModified();
        }
    }

    return latest;
}

} // namespace

namespace Hephaestus::Launcher {

LauncherProjectManager::LauncherProjectManager(QObject* parent)
    : QObject(parent)
{
}

LauncherSettings LauncherProjectManager::LoadSettings() const
{
    LauncherSettings settings;
    QSettings qsettings(QStringLiteral("Hephaestus"), QStringLiteral("Launcher"));

    settings.editorPath = qsettings.value(QStringLiteral("settings/editorPath")).toString();
    settings.runtimePath = qsettings.value(QStringLiteral("settings/runtimePath")).toString();
    settings.projectRootFolders = qsettings.value(QStringLiteral("settings/projectRoots")).toStringList();
    settings.theme = qsettings.value(QStringLiteral("settings/theme"), QStringLiteral("Dark")).toString();
    settings.openLastProjectOnStartup = qsettings.value(QStringLiteral("settings/openLastProjectOnStartup"), false).toBool();
    settings.lastOpenedProject = qsettings.value(QStringLiteral("settings/lastOpenedProject")).toString();

    if (settings.editorPath.isEmpty()) {
        settings.editorPath = DetectEditorPath();
    }
    if (settings.runtimePath.isEmpty()) {
        settings.runtimePath = DetectRuntimePath();
    }
    if (settings.projectRootFolders.isEmpty()) {
        QString docsRoot = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!docsRoot.isEmpty()) {
            settings.projectRootFolders << QDir(docsRoot).filePath(QStringLiteral("HephaestusProjects"));
        }
        settings.projectRootFolders << QDir::homePath();
        settings.projectRootFolders.removeDuplicates();
    }

    return settings;
}

void LauncherProjectManager::SaveSettings(const LauncherSettings& settings) const
{
    QSettings qsettings(QStringLiteral("Hephaestus"), QStringLiteral("Launcher"));
    qsettings.setValue(QStringLiteral("settings/editorPath"), settings.editorPath);
    qsettings.setValue(QStringLiteral("settings/runtimePath"), settings.runtimePath);
    qsettings.setValue(QStringLiteral("settings/projectRoots"), settings.projectRootFolders);
    qsettings.setValue(QStringLiteral("settings/theme"), settings.theme);
    qsettings.setValue(QStringLiteral("settings/openLastProjectOnStartup"), settings.openLastProjectOnStartup);
    qsettings.setValue(QStringLiteral("settings/lastOpenedProject"), settings.lastOpenedProject);
}

QString LauncherProjectManager::DetectEditorPath() const
{
    const QStringList candidates = {
        Hephaestus::Tools::PathUtils::SiblingExecutablePath(QStringLiteral("HephaestusEditor")),
        Hephaestus::Tools::PathUtils::SiblingExecutablePath(QStringLiteral("HephaestusEngine"))
    };

    const QString found = FirstExistingFile(candidates);
    return found.isEmpty() ? candidates.front() : found;
}

QString LauncherProjectManager::DetectRuntimePath() const
{
    const QStringList candidates = {
        Hephaestus::Tools::PathUtils::SiblingExecutablePath(QStringLiteral("HephaestusRuntime")),
        Hephaestus::Tools::PathUtils::SiblingExecutablePath(QStringLiteral("HephaestusGame"))
    };

    const QString found = FirstExistingFile(candidates);
    return found.isEmpty() ? candidates.front() : found;
}

bool LauncherProjectManager::ReadProject(const QString& projectFilePath, LauncherProject* outProject) const
{
    if (!outProject) {
        return false;
    }

    QFileInfo fileInfo(projectFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    QJsonObject root;
    if (!Hephaestus::Tools::JsonUtils::ReadJsonObject(fileInfo.absoluteFilePath(), &root)) {
        return false;
    }

    LauncherProject project;

    if (fileInfo.dir().dirName().compare(QStringLiteral(".hephaestus"), Qt::CaseInsensitive) == 0) {
        QDir projectDir = fileInfo.dir();
        projectDir.cdUp();
        project.projectDir = projectDir.absolutePath();
    } else {
        project.projectDir = fileInfo.dir().absolutePath();
    }

    const QString preferredProjectFile = QDir(project.projectDir).filePath(QStringLiteral("project.heproject"));
    if (QFileInfo::exists(preferredProjectFile)) {
        project.projectFilePath = preferredProjectFile;
    } else {
        project.projectFilePath = fileInfo.absoluteFilePath();
    }

    const QFileInfo projectDirInfo(project.projectDir);

    project.name = root.value(QStringLiteral("name")).toString(projectDirInfo.fileName());
    project.description = root.value(QStringLiteral("description")).toString();
    project.engineVersion = root.value(QStringLiteral("engineVersion")).toString(QStringLiteral("1.0.0"));
    project.favorite = root.value(QStringLiteral("isFavorite")).toBool(false);
    project.screenshotPath = root.value(QStringLiteral("screenshot")).toString();

    QJsonArray tagsArray = root.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue& value : tagsArray) {
        if (value.isString()) {
            project.tags.append(value.toString());
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    project.created = ReadEpochDate(root, QStringLiteral("created"), now);
    const QDateTime metadataLastModified = ReadEpochDate(root, QStringLiteral("lastModified"), fileInfo.lastModified());
    const QDateTime contentLastModified = ComputeProjectContentLastModified(project.projectDir, metadataLastModified);
    project.lastModified = (contentLastModified > metadataLastModified) ? contentLastModified : metadataLastModified;

    if (contentLastModified > metadataLastModified) {
        root[QStringLiteral("lastModified")] = contentLastModified.toSecsSinceEpoch();
        Hephaestus::Tools::JsonUtils::WriteJsonObject(project.projectFilePath, root);

        const QString secondaryPath = QDir(project.projectDir).filePath(QStringLiteral(".hephaestus/project.heproject"));
        Hephaestus::Tools::JsonUtils::WriteJsonObject(secondaryPath, root);
    }

    if (!project.screenshotPath.isEmpty()) {
        QFileInfo screenshotInfo(QDir(project.projectDir).filePath(project.screenshotPath));
        if (screenshotInfo.exists() && screenshotInfo.isFile()) {
            project.screenshotAbsolutePath = screenshotInfo.absoluteFilePath();
        }
    }

    *outProject = project;
    return true;
}

bool LauncherProjectManager::SetFavorite(const LauncherProject& project, bool favorite, QString* outError)
{
    LauncherProject updated = project;
    updated.favorite = favorite;
    updated.lastModified = QDateTime::currentDateTime();

    const QJsonObject root = BuildProjectJson(updated);
    if (!Hephaestus::Tools::JsonUtils::WriteJsonObject(updated.projectFilePath, root)) {
        if (outError) {
            *outError = QStringLiteral("Failed to write project file: %1").arg(updated.projectFilePath);
        }
        return false;
    }

    QDir projectDir(updated.projectDir);
    const QString secondaryPath = projectDir.filePath(QStringLiteral(".hephaestus/project.heproject"));
    Hephaestus::Tools::JsonUtils::WriteJsonObject(secondaryPath, root);
    return true;
}

QString LauncherProjectManager::ProjectFilePathForNewProject(const QString& projectDir) const
{
    return QDir(projectDir).filePath(QStringLiteral("project.heproject"));
}

bool LauncherProjectManager::CreateProject(const QString& projectName,
                                           const QString& locationFolder,
                                           const QString& engineVersion,
                                           bool createDefaultScene,
                                           LauncherProject* outProject,
                                           QString* outError)
{
    if (projectName.trimmed().isEmpty()) {
        if (outError) {
            *outError = QStringLiteral("Project name is required.");
        }
        return false;
    }

    QDir baseDir(locationFolder);
    if (!baseDir.exists()) {
        if (!QDir().mkpath(locationFolder)) {
            if (outError) {
                *outError = QStringLiteral("Failed to create location folder: %1").arg(locationFolder);
            }
            return false;
        }
    }

    const QString projectDirPath = baseDir.filePath(projectName);
    QDir projectDir(projectDirPath);
    if (projectDir.exists()) {
        if (outError) {
            *outError = QStringLiteral("Project folder already exists: %1").arg(projectDirPath);
        }
        return false;
    }

    if (!QDir().mkpath(projectDirPath)) {
        if (outError) {
            *outError = QStringLiteral("Failed to create project folder: %1").arg(projectDirPath);
        }
        return false;
    }

    QDir().mkpath(projectDir.filePath(QStringLiteral(".hephaestus")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/scenes")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/textures")));

    LauncherProject project;
    project.projectDir = projectDirPath;
    project.projectFilePath = ProjectFilePathForNewProject(projectDirPath);
    project.name = projectName;
    project.description = QStringLiteral("New Hephaestus project");
    project.engineVersion = engineVersion.isEmpty() ? QStringLiteral("1.0.0") : engineVersion;
    project.favorite = false;
    project.tags = {QStringLiteral("New")};
    project.created = QDateTime::currentDateTime();
    project.lastModified = project.created;

    if (createDefaultScene) {
        QFile defaultScene(projectDir.filePath(QStringLiteral("assets/scenes/default.hpscene")));
        if (defaultScene.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            defaultScene.write("{\n  \"version\": 1,\n  \"name\": \"DefaultScene\",\n  \"entities\": []\n}\n");
            defaultScene.close();
        }
    }

    const QJsonObject root = BuildProjectJson(project);
    if (!Hephaestus::Tools::JsonUtils::WriteJsonObject(project.projectFilePath, root)) {
        if (outError) {
            *outError = QStringLiteral("Failed to create project file: %1").arg(project.projectFilePath);
        }
        return false;
    }

    const QString legacyPath = projectDir.filePath(QStringLiteral(".hephaestus/project.heproject"));
    Hephaestus::Tools::JsonUtils::WriteJsonObject(legacyPath, root);

    if (outProject) {
        *outProject = project;
    }

    return true;
}

bool LauncherProjectManager::DeleteProject(const LauncherProject& project, QString* outError)
{
    if (project.projectDir.isEmpty()) {
        if (outError) {
            *outError = QStringLiteral("Project path is empty.");
        }
        return false;
    }

    QDir projectDir(project.projectDir);
    if (!projectDir.exists()) {
        return true;
    }

    if (!projectDir.removeRecursively()) {
        if (outError) {
            *outError = QStringLiteral("Failed to delete project: %1").arg(project.projectDir);
        }
        return false;
    }

    return true;
}

bool LauncherProjectManager::OpenProjectInEditor(const LauncherProject& project, const QString& editorPath, QString* outError) const
{
    QFileInfo editorInfo(editorPath);
    if (!editorInfo.exists() || !editorInfo.isFile()) {
        if (outError) {
            *outError = QStringLiteral("Editor executable not found: %1").arg(editorPath);
        }
        return false;
    }

    QStringList args;
    if (!project.projectFilePath.isEmpty()) {
        args << project.projectFilePath;
    }

    if (!QProcess::startDetached(editorInfo.absoluteFilePath(), args)) {
        if (outError) {
            *outError = QStringLiteral("Failed to launch editor: %1").arg(editorInfo.absoluteFilePath());
        }
        return false;
    }

    return true;
}

bool LauncherProjectManager::OpenInExplorer(const QString& path) const
{
    if (path.isEmpty()) {
        return false;
    }

#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), {path});
#else
    return QProcess::startDetached(QStringLiteral("xdg-open"), {path});
#endif
}

} // namespace Hephaestus::Launcher
