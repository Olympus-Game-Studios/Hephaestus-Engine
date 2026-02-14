#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace Hephaestus::Tools {

enum class SettingControlType {
    Bool,
    Int,
    Float,
    Enum,
    Path,
    String
};

struct SettingDefinition {
    QString id;
    QString categoryId;
    QString categoryLabel;
    QString key;
    QString label;
    SettingControlType controlType = SettingControlType::String;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    QStringList enumOptions;
};

class SettingsSchemaRegistry {
public:
    static SettingsSchemaRegistry& Instance();

    const QVector<SettingDefinition>& EngineSettings() const;
    const QVector<SettingDefinition>& ProjectSettings() const;

    void RegisterEngineSetting(const SettingDefinition& definition);
    void RegisterProjectSetting(const SettingDefinition& definition);

private:
    SettingsSchemaRegistry();
    void RegisterBuiltins();
    static bool ContainsId(const QVector<SettingDefinition>& definitions, const QString& id);

    QVector<SettingDefinition> m_engineDefinitions;
    QVector<SettingDefinition> m_projectDefinitions;
};

} // namespace Hephaestus::Tools
