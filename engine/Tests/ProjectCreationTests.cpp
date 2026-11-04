#include "ToolsCommon/ProjectCreationService.h"
#include "ToolsCommon/ProjectSettingsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

bool DirExists(const QDir& root, const QString& relative)
{
    return QDir(root.filePath(relative)).exists();
}

bool FileExists(const QDir& root, const QString& relative)
{
    return QFileInfo::exists(root.filePath(relative));
}

bool TestCreateBlankProjectLayout()
{
    QTemporaryDir temp;
    if (!Check(temp.isValid(), "temp dir valid")) {
        return false;
    }

    Hephaestus::Tools::ProjectCreationService::Result result;
    QString error;
    const bool ok = Hephaestus::Tools::ProjectCreationService::CreateBlankProject(
        temp.path(),
        QStringLiteral("LayoutTestProj"),
        QStringLiteral("9.9.9-test"),
        true,
        &result,
        &error);

    if (!Check(ok, "CreateBlankProject succeeded: " + error.toStdString())) {
        return false;
    }

    QDir project(result.projectDir);
    bool pass = true;
    pass &= Check(DirExists(project, QStringLiteral(".hephaestus")), ".hephaestus/");
    pass &= Check(DirExists(project, QStringLiteral("assets/scenes")), "assets/scenes/");
    pass &= Check(DirExists(project, QStringLiteral("assets/textures")), "assets/textures/");
    pass &= Check(DirExists(project, QStringLiteral("assets/scripts")), "assets/scripts/");
    pass &= Check(DirExists(project, QStringLiteral("assets/materials")), "assets/materials/");
    pass &= Check(DirExists(project, QStringLiteral("assets/prefabs")), "assets/prefabs/");
    pass &= Check(DirExists(project, QStringLiteral("assets/ui")), "assets/ui/");
    pass &= Check(DirExists(project, QStringLiteral("assets/input")), "assets/input/");
    pass &= Check(FileExists(project, QStringLiteral("project.heproject")), "project.heproject");
    pass &= Check(FileExists(project, QStringLiteral(".hephaestus/project.heproject")),
                  ".hephaestus/project.heproject");
    pass &= Check(FileExists(project, QStringLiteral("assets/scenes/default.hpscene")),
                  "assets/scenes/default.hpscene");
    pass &= Check(FileExists(project, QStringLiteral("assets/input/Default.hpinput")),
                  "assets/input/Default.hpinput");
    pass &= Check(FileExists(project, QStringLiteral(".hephaestus/physics_layers.json")),
                  ".hephaestus/physics_layers.json");
    pass &= Check(result.defaultSceneRelativePath == QStringLiteral("assets/scenes/default.hpscene"),
                  "defaultSceneRelativePath");

    QFile projectFile(result.projectFilePath);
    if (!Check(projectFile.open(QIODevice::ReadOnly), "open project.heproject")) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
    projectFile.close();
    pass &= Check(root.value(QStringLiteral("name")).toString() == QStringLiteral("LayoutTestProj"),
                  "project name field");
    pass &= Check(root.value(QStringLiteral("engineVersion")).toString() == QStringLiteral("9.9.9-test"),
                  "engineVersion field");
    pass &= Check(root.value(QStringLiteral("defaultScene")).toString() ==
                      QStringLiteral("assets/scenes/default.hpscene"),
                  "defaultScene field");

    // Second create into same folder should fail
    QString dupError;
    const bool dup = Hephaestus::Tools::ProjectCreationService::CreateBlankProject(
        temp.path(), QStringLiteral("LayoutTestProj"), QString(), true, nullptr, &dupError);
    pass &= Check(!dup, "duplicate project rejected");
    pass &= Check(!dupError.isEmpty(), "duplicate error message set");

    return pass;
}

bool TestEditorModeRoundTrip()
{
    QTemporaryDir temp;
    if (!Check(temp.isValid(), "temp dir valid")) {
        return false;
    }

    Hephaestus::Tools::ProjectCreationService::Result result;
    QString error;
    if (!Check(Hephaestus::Tools::ProjectCreationService::CreateBlankProject(
                   temp.path(), QStringLiteral("ModeProj"), QString(), false, &result, &error,
                   QStringLiteral("programmer")),
               "CreateBlankProject succeeded: " + error.toStdString())) {
        return false;
    }

    const Hephaestus::Tools::ProjectSettingsManager settings(result.projectDir);
    bool pass = true;
    pass &= Check(settings.EditorMode() == QStringLiteral("programmer"), "new project starts in the chosen mode");
    pass &= Check(settings.SetEditorMode(QStringLiteral("artist")), "SetEditorMode succeeded");
    pass &= Check(settings.EditorMode() == QStringLiteral("artist"), "editor mode reads back");

    QDir project(result.projectDir);
    for (const QString& relative : { QStringLiteral("project.heproject"), QStringLiteral(".hephaestus/project.heproject") }) {
        QFile file(project.filePath(relative));
        if (!file.open(QIODevice::ReadOnly)) {
            pass &= Check(false, "read " + relative.toStdString());
            continue;
        }
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        pass &= Check(root.value(QStringLiteral("editorMode")).toString() == QStringLiteral("artist"),
                      "editorMode written to " + relative.toStdString());
        pass &= Check(root.value(QStringLiteral("name")).toString() == QStringLiteral("ModeProj"),
                      "other metadata kept in " + relative.toStdString());
    }

    pass &= Check(!Hephaestus::Tools::ProjectSettingsManager(temp.filePath(QStringLiteral("missing"))).SetEditorMode(QStringLiteral("artist")),
                  "SetEditorMode fails without a project file");
    return pass;
}

bool TestRecordEngineVersion()
{
    QTemporaryDir temp;
    if (!Check(temp.isValid(), "temp dir valid")) {
        return false;
    }

    Hephaestus::Tools::ProjectCreationService::Result result;
    QString error;
    if (!Check(Hephaestus::Tools::ProjectCreationService::CreateBlankProject(
                   temp.path(), QStringLiteral("VersionProj"), QStringLiteral("2026.10.5"), false, &result, &error),
               "CreateBlankProject succeeded: " + error.toStdString())) {
        return false;
    }

    QDir project(result.projectDir);
    auto recordedIn = [&](const QString& relative) {
        QFile file(project.filePath(relative));
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("engineVersion")).toString();
    };

    const Hephaestus::Tools::ProjectSettingsManager settings(result.projectDir);
    bool pass = true;
    pass &= Check(settings.RecordEngineVersion(QStringLiteral("2026.9.30")) == QStringLiteral("2026.10.5"),
                  "an older engine is told the newer version that opened the project");
    pass &= Check(recordedIn(QStringLiteral("project.heproject")) == QStringLiteral("2026.10.5"),
                  "an older engine does not lower the recorded version");
    pass &= Check(settings.RecordEngineVersion(QStringLiteral("2026.11.3")).isEmpty(),
                  "a newer engine opens without a warning");
    for (const QString& relative : { QStringLiteral("project.heproject"), QStringLiteral(".hephaestus/project.heproject") }) {
        pass &= Check(recordedIn(relative) == QStringLiteral("2026.11.3"),
                      "a newer engine records its version in " + relative.toStdString());
    }
    pass &= Check(settings.RecordEngineVersion(QStringLiteral("2026.11.3")).isEmpty(),
                  "the same engine opens without a warning");
    return pass;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    ok &= TestCreateBlankProjectLayout();
    ok &= TestEditorModeRoundTrip();
    ok &= TestRecordEngineVersion();
    if (!ok) {
        std::cerr << "ProjectCreationTests FAILED\n";
        return 1;
    }
    std::cout << "ProjectCreationTests PASSED\n";
    return 0;
}
