#include "ProjectSettingsManager.h"

#include "JsonUtils.h"

#include <algorithm>
#include <utility>
#include <QDir>
#include <QFileInfo>
#include <QJsonValue>
#include <QVersionNumber>
#include <QtGlobal>

namespace {

QVariant JsonToVariant(const QJsonValue& value)
{
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        return value.toString();
    }
    return {};
}

QJsonValue VariantToJson(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return QJsonValue(value.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return QJsonValue(value.toDouble());
    default:
        return QJsonValue(value.toString());
    }
}

QString PrimaryProjectFilePath(const QString& projectRoot)
{
    return QDir(projectRoot).filePath(QStringLiteral("project.heproject"));
}

QString SecondaryProjectFilePath(const QString& projectRoot)
{
    return QDir(projectRoot).filePath(QStringLiteral(".hephaestus/project.heproject"));
}

QString ResolveProjectFilePath(const QString& projectRoot)
{
    const QString primary = PrimaryProjectFilePath(projectRoot);
    const QString secondary = SecondaryProjectFilePath(projectRoot);

    if (QFileInfo::exists(primary)) {
        return primary;
    }
    if (QFileInfo::exists(secondary)) {
        return secondary;
    }
    return primary;
}

void ApplyProjectInfoFromMetadata(QHash<QString, QVariant>& values, const QJsonObject& projectMetadata)
{
    const QJsonValue nameValue = projectMetadata.value(QStringLiteral("name"));
    if (nameValue.isString()) {
        values[QStringLiteral("project.info.name")] = nameValue.toString();
    }

    const QJsonValue descriptionValue = projectMetadata.value(QStringLiteral("description"));
    if (descriptionValue.isString()) {
        values[QStringLiteral("project.info.description")] = descriptionValue.toString();
    }

    const QJsonValue versionValue = projectMetadata.value(QStringLiteral("version"));
    if (versionValue.isString()) {
        values[QStringLiteral("project.info.version")] = versionValue.toString();
    } else {
        const QJsonValue engineVersionValue = projectMetadata.value(QStringLiteral("engineVersion"));
        if (engineVersionValue.isString()) {
            values[QStringLiteral("project.info.version")] = engineVersionValue.toString();
        }
    }

    const QJsonValue companyNameValue = projectMetadata.value(QStringLiteral("companyName"));
    if (companyNameValue.isString()) {
        values[QStringLiteral("project.info.companyName")] = companyNameValue.toString();
    }

    const QString defaultSceneId = QStringLiteral("project.info.defaultScene");
    const QJsonValue defaultSceneValue = projectMetadata.value(QStringLiteral("defaultScene"));
    if (values.value(defaultSceneId).toString().trimmed().isEmpty() && defaultSceneValue.isString()) {
        values[defaultSceneId] = defaultSceneValue.toString();
    }
}

} // namespace

namespace Hephaestus::Tools {

ProjectSettingsManager::ProjectSettingsManager(const QString& projectRoot)
    : m_projectRoot(projectRoot)
{
    const auto& defs = Definitions();
    for (const SettingDefinition& def : defs) {
        m_values.insert(def.id, def.defaultValue);
    }
}

void ProjectSettingsManager::SetProjectRoot(const QString& projectRoot)
{
    m_projectRoot = projectRoot;
}

bool ProjectSettingsManager::IsAvailable() const
{
    return !m_projectRoot.trimmed().isEmpty();
}

bool ProjectSettingsManager::Load()
{
    const auto& defs = Definitions();
    for (const SettingDefinition& def : defs) {
        m_values[def.id] = def.defaultValue;
    }

    if (!IsAvailable()) {
        m_loadedRoot = QJsonObject();
        return false;
    }

    QJsonObject root;
    if (!JsonUtils::ReadJsonObject(SettingsFilePath(), &root)) {
        m_loadedRoot = QJsonObject();
    } else {
        m_loadedRoot = root;

        for (const SettingDefinition& def : defs) {
            const QJsonObject categoryObject = root.value(def.categoryId).toObject();
            if (categoryObject.contains(def.key)) {
                m_values[def.id] = Validate(def, JsonToVariant(categoryObject.value(def.key)));
            }
        }
    }

    QJsonObject projectMetadata;
    const QString projectFilePath = ResolveProjectFilePath(m_projectRoot);
    if (JsonUtils::ReadJsonObject(projectFilePath, &projectMetadata)) {
        ApplyProjectInfoFromMetadata(m_values, projectMetadata);
    }

    return true;
}

bool ProjectSettingsManager::Save() const
{
    if (!IsAvailable()) {
        return false;
    }

    QJsonObject output = m_loadedRoot;

    output[QStringLiteral("settingsVersion")] = kSettingsVersion;

    const auto& defs = Definitions();
    for (const SettingDefinition& def : defs) {
        const bool isProjectMetadataField =
            def.id == QStringLiteral("project.info.name") ||
            def.id == QStringLiteral("project.info.description") ||
            def.id == QStringLiteral("project.info.version") ||
            def.id == QStringLiteral("project.info.companyName");

        if (isProjectMetadataField) {
            continue;
        }
        QJsonObject categoryObject = output.value(def.categoryId).toObject();
        categoryObject[def.key] = VariantToJson(m_values.value(def.id, def.defaultValue));
        output[def.categoryId] = categoryObject;
    }

    if (!JsonUtils::WriteJsonObject(SettingsFilePath(), output)) {
        return false;
    }

    const QString projectFilePath = ResolveProjectFilePath(m_projectRoot);
    QJsonObject projectMetadata;
    JsonUtils::ReadJsonObject(projectFilePath, &projectMetadata);

    if (!projectMetadata.contains(QStringLiteral("formatVersion"))) {
        projectMetadata[QStringLiteral("formatVersion")] = 1;
    }

    projectMetadata[QStringLiteral("name")] = m_values.value(QStringLiteral("project.info.name")).toString();
    projectMetadata[QStringLiteral("description")] = m_values.value(QStringLiteral("project.info.description")).toString();
    projectMetadata[QStringLiteral("version")] = m_values.value(QStringLiteral("project.info.version")).toString();
    projectMetadata[QStringLiteral("companyName")] = m_values.value(QStringLiteral("project.info.companyName")).toString();

    if (!projectMetadata.contains(QStringLiteral("engineVersion"))) {
        projectMetadata[QStringLiteral("engineVersion")] = QStringLiteral("1.0.0");
    }

    if (!JsonUtils::WriteJsonObject(projectFilePath, projectMetadata)) {
        return false;
    }

    const QString secondaryPath = SecondaryProjectFilePath(m_projectRoot);
    if (secondaryPath != projectFilePath) {
        JsonUtils::WriteJsonObject(secondaryPath, projectMetadata);
    }

    return true;
}

QString ProjectSettingsManager::EditorMode() const
{
    QJsonObject projectMetadata;
    if (!IsAvailable() || !JsonUtils::ReadJsonObject(ResolveProjectFilePath(m_projectRoot), &projectMetadata)) {
        return {};
    }
    return projectMetadata.value(QStringLiteral("editorMode")).toString();
}

bool ProjectSettingsManager::SetEditorMode(const QString& mode) const
{
    return SetProjectMetadata(QStringLiteral("editorMode"), mode);
}

QString ProjectSettingsManager::RecordEngineVersion(const QString& engineVersion) const
{
    if (!IsAvailable()) {
        return {};
    }
    const QString projectFilePath = ResolveProjectFilePath(m_projectRoot);
    QJsonObject projectMetadata;
    if (!JsonUtils::ReadJsonObject(projectFilePath, &projectMetadata)) {
        return {};
    }
    QList<std::pair<QString, QJsonObject>> files{ { projectFilePath, projectMetadata } };
    const QString secondaryPath = SecondaryProjectFilePath(m_projectRoot);
    QJsonObject secondaryMetadata;
    if (secondaryPath != projectFilePath && JsonUtils::ReadJsonObject(secondaryPath, &secondaryMetadata)) {
        files.append({ secondaryPath, secondaryMetadata });
    }

    const auto versionIn = [](const QJsonObject& metadata) {
        return metadata.value(QStringLiteral("engineVersion")).toString();
    };
    QString newest = engineVersion;
    for (const auto& [path, metadata] : files) {
        if (QVersionNumber::fromString(versionIn(metadata)) > QVersionNumber::fromString(newest)) {
            newest = versionIn(metadata);
        }
    }
    if (newest != engineVersion) {
        return newest;
    }

    for (auto& [path, metadata] : files) {
        if (versionIn(metadata) == engineVersion) {
            continue;
        }
        metadata[QStringLiteral("engineVersion")] = engineVersion;
        if (!JsonUtils::WriteJsonObject(path, metadata)) {
            qWarning("Could not record engine version %s in %s", qUtf8Printable(engineVersion),
                     qUtf8Printable(path));
        }
    }
    return {};
}

bool ProjectSettingsManager::SetProjectMetadata(const QString& key, const QJsonValue& value) const
{
    if (!IsAvailable()) {
        return false;
    }

    const QString projectFilePath = ResolveProjectFilePath(m_projectRoot);
    QJsonObject projectMetadata;
    if (!JsonUtils::ReadJsonObject(projectFilePath, &projectMetadata)) {
        return false;
    }
    projectMetadata[key] = value;
    if (!JsonUtils::WriteJsonObject(projectFilePath, projectMetadata)) {
        return false;
    }

    const QString secondaryPath = SecondaryProjectFilePath(m_projectRoot);
    QJsonObject secondaryMetadata;
    if (secondaryPath != projectFilePath && JsonUtils::ReadJsonObject(secondaryPath, &secondaryMetadata)) {
        secondaryMetadata[key] = value;
        JsonUtils::WriteJsonObject(secondaryPath, secondaryMetadata);
    }
    return true;
}

QString ProjectSettingsManager::SettingsFilePath() const
{
    return QDir(m_projectRoot).filePath(QStringLiteral(".hephaestus/project_settings.json"));
}

const QVector<SettingDefinition>& ProjectSettingsManager::Definitions() const
{
    return SettingsSchemaRegistry::Instance().ProjectSettings();
}

QStringList ProjectSettingsManager::CategoryIds() const
{
    QStringList categories;
    for (const SettingDefinition& def : Definitions()) {
        if (!categories.contains(def.categoryId)) {
            categories.push_back(def.categoryId);
        }
    }
    return categories;
}

QString ProjectSettingsManager::CategoryLabel(const QString& categoryId) const
{
    for (const SettingDefinition& def : Definitions()) {
        if (def.categoryId == categoryId) {
            return def.categoryLabel;
        }
    }
    return categoryId;
}

QVariant ProjectSettingsManager::Value(const QString& id) const
{
    return m_values.value(id);
}

QHash<QString, QVariant> ProjectSettingsManager::Values() const
{
    return m_values;
}

void ProjectSettingsManager::SetValue(const QString& id, const QVariant& value)
{
    for (const SettingDefinition& def : Definitions()) {
        if (def.id == id) {
            m_values[id] = Validate(def, value);
            return;
        }
    }
}

void ProjectSettingsManager::SetValues(const QHash<QString, QVariant>& values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        SetValue(it.key(), it.value());
    }
}

void ProjectSettingsManager::ResetCategoryToDefaults(const QString& categoryId)
{
    for (const SettingDefinition& def : Definitions()) {
        if (def.categoryId == categoryId) {
            m_values[def.id] = def.defaultValue;
        }
    }
}

QVariant ProjectSettingsManager::Validate(const SettingDefinition& definition, const QVariant& value)
{
    switch (definition.controlType) {
    case SettingControlType::Bool:
        return value.toBool();
    case SettingControlType::Int: {
        int out = value.toInt();
        if (definition.minValue.isValid()) {
            out = std::max(out, definition.minValue.toInt());
        }
        if (definition.maxValue.isValid()) {
            out = std::min(out, definition.maxValue.toInt());
        }
        return out;
    }
    case SettingControlType::Float: {
        double out = value.toDouble();
        if (definition.minValue.isValid()) {
            out = std::max(out, definition.minValue.toDouble());
        }
        if (definition.maxValue.isValid()) {
            out = std::min(out, definition.maxValue.toDouble());
        }
        return out;
    }
    case SettingControlType::Enum: {
        const QString candidate = value.toString();
        if (definition.enumOptions.contains(candidate)) {
            return candidate;
        }
        return definition.defaultValue;
    }
    case SettingControlType::Path:
    case SettingControlType::String:
    default:
        return value.toString();
    }
}

} // namespace Hephaestus::Tools
