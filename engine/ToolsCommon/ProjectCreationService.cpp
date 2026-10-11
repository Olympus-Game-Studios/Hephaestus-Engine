#include "ProjectCreationService.h"

#include "JsonUtils.h"
#include "PathUtils.h"
#include "ProjectSettingsManager.h"
#include "VersionInfo.h"
#include "../Input/InputMap.h"
#include "../Physics/PhysicsLayerConfig.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace Hephaestus::Tools {

namespace {

constexpr const char* kDefaultSceneRelative = "assets/scenes/default.hpscene";
constexpr const char* kDefaultHdriDest = ".engine/hdris/studio_garden_1k.hdr";

bool TryCopyDefaultHdri(const QDir& projectDir, QString* warningOut)
{
    const QString exeDir = PathUtils::ExecutableDir();
    const QStringList candidates = {
        QDir(exeDir).filePath(QStringLiteral(".engine/hdris/studio_garden_1k.hdr")),
        QDir(exeDir).filePath(QStringLiteral("assets/textures/hdris/studio_garden_1k.hdr")),
    };

    QString srcPath;
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            srcPath = candidate;
            break;
        }
    }
    if (srcPath.isEmpty()) {
        if (warningOut) {
            *warningOut = QStringLiteral("Default HDRI source not found near executable.");
        }
        return false;
    }

    const QString dstPath = projectDir.filePath(QString::fromUtf8(kDefaultHdriDest));
    if (!QDir().mkpath(QFileInfo(dstPath).absolutePath())) {
        if (warningOut) {
            *warningOut = QStringLiteral("Failed to create HDRI folder.");
        }
        return false;
    }
    if (QFile::exists(dstPath)) {
        return true;
    }
    if (!QFile::copy(srcPath, dstPath)) {
        if (warningOut) {
            *warningOut = QStringLiteral("Failed to copy default HDRI.");
        }
        return false;
    }
    return true;
}

} // namespace

bool ProjectCreationService::CreateBlankProject(const QString& parentDir,
                                                const QString& name,
                                                const QString& engineVersion,
                                                bool createDefaultScene,
                                                Result* outResult,
                                                QString* outError)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        if (outError) {
            *outError = QStringLiteral("Project name is required.");
        }
        return false;
    }

    QDir baseDir(parentDir);
    if (!baseDir.exists()) {
        if (!QDir().mkpath(parentDir)) {
            if (outError) {
                *outError = QStringLiteral("Failed to create location folder: %1").arg(parentDir);
            }
            return false;
        }
    }

    const QString projectDirPath = baseDir.filePath(trimmedName);
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
    {
        QFile layerFile(projectDir.filePath(QStringLiteral(".hephaestus/physics_layers.json")));
        if (layerFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            layerFile.write(Physics::kPhysicsLayerSeedJson);
            layerFile.close();
        }
    }
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/scenes")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/textures")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/scripts")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/materials")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/prefabs")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/ui")));
    QDir().mkpath(projectDir.filePath(QStringLiteral("assets/input")));

    std::string inputMapError;
    const QString inputMapPath =
        projectDir.filePath(QStringLiteral("assets/input/Default.hpinput"));
    if (!Hephaestus::InputMap::CreateDefault().Save(
            std::filesystem::path(inputMapPath.toStdWString()), inputMapError)) {
        if (outError) {
            *outError = QStringLiteral("Failed to create default input map: %1")
                            .arg(QString::fromStdString(inputMapError));
        }
        return false;
    }

    // HDRI is optional convenience content — never fail project creation.
    QString hdriWarning;
    TryCopyDefaultHdri(projectDir, &hdriWarning);
    Q_UNUSED(hdriWarning);

    QString defaultSceneRelative;
    if (createDefaultScene) {
        defaultSceneRelative = QString::fromUtf8(kDefaultSceneRelative);
        QFile defaultScene(projectDir.filePath(defaultSceneRelative));
        if (defaultScene.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            defaultScene.write("{\n  \"version\": 1,\n  \"name\": \"DefaultScene\",\n  \"entities\": []\n}\n");
            defaultScene.close();
        }
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString version = engineVersion.isEmpty() ? VersionInfo::EngineVersion() : engineVersion;

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 1);
    root.insert(QStringLiteral("name"), trimmedName);
    root.insert(QStringLiteral("description"), QStringLiteral("New Hephaestus project"));
    root.insert(QStringLiteral("engineVersion"), version);
    root.insert(QStringLiteral("created"), static_cast<double>(now.toSecsSinceEpoch()));
    root.insert(QStringLiteral("lastModified"), static_cast<double>(now.toSecsSinceEpoch()));
    if (!defaultSceneRelative.isEmpty()) {
        root.insert(QStringLiteral("defaultScene"), defaultSceneRelative);
    }

    const QString projectFile = projectDir.filePath(QStringLiteral("project.heproject"));
    if (!JsonUtils::WriteJsonObject(projectFile, root)) {
        if (outError) {
            *outError = QStringLiteral("Failed to create project file: %1").arg(projectFile);
        }
        return false;
    }
    JsonUtils::WriteJsonObject(projectDir.filePath(QStringLiteral(".hephaestus/project.heproject")), root);

    if (outResult) {
        outResult->projectDir = projectDirPath;
        outResult->projectFilePath = projectFile;
        outResult->defaultSceneRelativePath = defaultSceneRelative;
    }
    return true;
}

bool ProjectCreationService::SetLastSceneRelative(ProjectSettingsManager& settings,
                                                  const QString& relativeScenePath)
{
    if (!settings.IsAvailable()) {
        return false;
    }
    const QString path = relativeScenePath.trimmed();
    if (path.isEmpty()) {
        return false;
    }
    settings.SetValue(QStringLiteral("project.info.lastScene"), path);
    settings.SetValue(QStringLiteral("project.info.defaultScene"), path);
    return settings.Save();
}

} // namespace Hephaestus::Tools
