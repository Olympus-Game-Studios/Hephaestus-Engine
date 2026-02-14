#include "EngineSettingsManager.h"

#include "JsonUtils.h"

#include <algorithm>
#include <QDir>
#include <QJsonValue>
#include <QStandardPaths>

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

} // namespace

namespace Hephaestus::Tools {

EngineSettingsManager::EngineSettingsManager()
{
    const auto& defs = Definitions();
    for (const SettingDefinition& def : defs) {
        m_values.insert(def.id, def.defaultValue);
    }
}

bool EngineSettingsManager::Load()
{
    const auto& defs = Definitions();
    for (const SettingDefinition& def : defs) {
        m_values[def.id] = def.defaultValue;
    }

    QJsonObject root;
    if (!JsonUtils::ReadJsonObject(SettingsFilePath(), &root)) {
        m_loadedRoot = QJsonObject();
        return true;
    }

    m_loadedRoot = root;

    for (const SettingDefinition& def : defs) {
        const QJsonObject categoryObject = root.value(def.categoryId).toObject();
        if (categoryObject.contains(def.key)) {
            m_values[def.id] = Validate(def, JsonToVariant(categoryObject.value(def.key)));
        }
    }

    return true;
}

bool EngineSettingsManager::Save() const
{
    QJsonObject output = m_loadedRoot;

    output[QStringLiteral("settingsVersion")] = kSettingsVersion;

    const auto& defs = Definitions();
    for (const SettingDefinition& def : defs) {
        QJsonObject categoryObject = output.value(def.categoryId).toObject();
        categoryObject[def.key] = VariantToJson(m_values.value(def.id, def.defaultValue));
        output[def.categoryId] = categoryObject;
    }

    return JsonUtils::WriteJsonObject(SettingsFilePath(), output);
}

QString EngineSettingsManager::SettingsFilePath() const
{
    QString basePath = QDir::homePath();
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        basePath = appData;
    }

    return QDir(basePath).filePath(QStringLiteral(".hephaestus/engine_settings.json"));
}

const QVector<SettingDefinition>& EngineSettingsManager::Definitions() const
{
    return SettingsSchemaRegistry::Instance().EngineSettings();
}

QStringList EngineSettingsManager::CategoryIds() const
{
    QStringList categories;
    for (const SettingDefinition& def : Definitions()) {
        if (!categories.contains(def.categoryId)) {
            categories.push_back(def.categoryId);
        }
    }
    return categories;
}

QString EngineSettingsManager::CategoryLabel(const QString& categoryId) const
{
    for (const SettingDefinition& def : Definitions()) {
        if (def.categoryId == categoryId) {
            return def.categoryLabel;
        }
    }
    return categoryId;
}

QVariant EngineSettingsManager::Value(const QString& id) const
{
    return m_values.value(id);
}

QHash<QString, QVariant> EngineSettingsManager::Values() const
{
    return m_values;
}

void EngineSettingsManager::SetValue(const QString& id, const QVariant& value)
{
    for (const SettingDefinition& def : Definitions()) {
        if (def.id == id) {
            m_values[id] = Validate(def, value);
            return;
        }
    }
}

void EngineSettingsManager::SetValues(const QHash<QString, QVariant>& values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        SetValue(it.key(), it.value());
    }
}

void EngineSettingsManager::ResetCategoryToDefaults(const QString& categoryId)
{
    for (const SettingDefinition& def : Definitions()) {
        if (def.categoryId == categoryId) {
            m_values[def.id] = def.defaultValue;
        }
    }
}

QVariant EngineSettingsManager::Validate(const SettingDefinition& definition, const QVariant& value)
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
