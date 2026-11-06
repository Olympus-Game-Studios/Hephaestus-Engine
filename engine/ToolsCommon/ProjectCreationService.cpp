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
#include <QJsonArray>
#include <QJsonDocument>
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

QJsonArray Vec3Json(double x, double y, double z)
{
    return QJsonArray{x, y, z};
}

QJsonObject TransformJson(const QJsonArray& position, const QJsonArray& rotation, const QJsonArray& scale)
{
    return QJsonObject{{"position", position}, {"rotation", rotation}, {"scale", scale}};
}

QJsonObject EntityJson(int id, const QString& name, const QJsonObject& components)
{
    return QJsonObject{{"id", id}, {"name", name}, {"parent", QJsonValue::Null}, {"components", components}};
}

QByteArray BuildDefaultSceneJson(bool hasHdri)
{
    QJsonObject environment{
        {"type", hasHdri ? "HDRI" : "SolidColor"},
        {"solidColor", Vec3Json(0.45, 0.55, 0.7)},
        {"hdriPath", hasHdri ? QString::fromUtf8(kDefaultHdriDest) : QString()},
        {"hdriIntensity", 1.0},
        {"useAsIBL", true},
        {"reflectionIntensity", 1.0}};
    QJsonObject sun{
        {"Transform", TransformJson(Vec3Json(0.0, 10.0, 0.0), Vec3Json(-0.9, 0.6, 0.0), Vec3Json(1.0, 1.0, 1.0))},
        {"Light", QJsonObject{{"type", "Directional"}, {"enabled", true}, {"color", Vec3Json(1.0, 0.96, 0.9)},
                              {"intensity", 1.0}, {"castsShadow", true}}}};
    QJsonObject floor{
        {"Transform", TransformJson(Vec3Json(0.0, 0.0, 0.0), Vec3Json(0.0, 0.0, 0.0), Vec3Json(20.0, 1.0, 20.0))},
        {"MeshRenderer", QJsonObject{{"visible", true}, {"meshSource", "builtin://plane"}, {"submeshCount", 1},
                                     {"materialIndices", QJsonArray{0}},
                                     {"localBounds", QJsonObject{{"min", Vec3Json(-0.75, 0.0, -0.75)},
                                                                 {"max", Vec3Json(0.75, 0.0, 0.75)}}}}},
        {"Material", QJsonObject{{"albedo", Vec3Json(0.6, 0.6, 0.6)}, {"metallic", 0.0}, {"roughness", 0.9}}},
        {"PhysicsShape", QJsonObject{{"type", "Box"}, {"extents", Vec3Json(0.75, 0.05, 0.75)},
                                     {"offset", Vec3Json(0.0, -0.05, 0.0)}}},
        {"RigidBody", QJsonObject{{"isStatic", true}, {"motion", "static"}}}};
    QJsonObject camera{
        {"Transform", TransformJson(Vec3Json(0.0, 2.0, 8.0), Vec3Json(-0.15, 0.0, 0.0), Vec3Json(1.0, 1.0, 1.0))},
        {"Camera", QJsonObject{{"fov", 60.0}, {"near", 0.1}, {"far", 1000.0},
                               {"projection", "Perspective"}, {"isActive", true}}}};

    const QJsonObject root{
        {"scene", QJsonObject{{"name", "DefaultScene"}, {"version", "1.0"}}},
        {"entities", QJsonArray{
            EntityJson(1, QStringLiteral("Environment"), QJsonObject{{"Environment", environment}}),
            EntityJson(2, QStringLiteral("Sun"), sun),
            EntityJson(3, QStringLiteral("Floor"), floor),
            EntityJson(4, QStringLiteral("Main Camera"), camera)}}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace

bool ProjectCreationService::CreateBlankProject(const QString& parentDir,
                                                const QString& name,
                                                const QString& engineVersion,
                                                bool createDefaultScene,
                                                Result* outResult,
                                                QString* outError,
                                                const QString& editorMode)
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
    const bool hasHdri = TryCopyDefaultHdri(projectDir, &hdriWarning);
    Q_UNUSED(hdriWarning);

    QString defaultSceneRelative;
    if (createDefaultScene) {
        defaultSceneRelative = QString::fromUtf8(kDefaultSceneRelative);
        QFile defaultScene(projectDir.filePath(defaultSceneRelative));
        if (defaultScene.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            defaultScene.write(BuildDefaultSceneJson(hasHdri));
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
    if (!editorMode.isEmpty()) {
        root.insert(QStringLiteral("editorMode"), editorMode);
    }

    const QString projectFile = projectDir.filePath(QStringLiteral("project.heproject"));
    if (!JsonUtils::WriteJsonObject(projectFile, root)) {
        if (outError) {
            *outError = QStringLiteral("Failed to create project file: %1").arg(projectFile);
        }
        return false;
    }
    JsonUtils::WriteJsonObject(projectDir.filePath(QStringLiteral(".hephaestus/project.heproject")), root);

    if (hasHdri) {
        ProjectSettingsManager settings(projectDirPath);
        settings.Load();
        settings.SetValue(QStringLiteral("project.rendering.defaultSkybox"), QString::fromUtf8(kDefaultHdriDest));
        settings.Save();
    }

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
