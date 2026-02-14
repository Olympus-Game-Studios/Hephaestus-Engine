#pragma once

#include "SettingsSchema.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace Hephaestus::Tools {

class EngineSettingsManager {
public:
    static constexpr int kSettingsVersion = 1;

    EngineSettingsManager();

    bool Load();
    bool Save() const;

    QString SettingsFilePath() const;

    const QVector<SettingDefinition>& Definitions() const;
    QStringList CategoryIds() const;
    QString CategoryLabel(const QString& categoryId) const;

    QVariant Value(const QString& id) const;
    QHash<QString, QVariant> Values() const;

    void SetValue(const QString& id, const QVariant& value);
    void SetValues(const QHash<QString, QVariant>& values);
    void ResetCategoryToDefaults(const QString& categoryId);

private:
    static QVariant Validate(const SettingDefinition& definition, const QVariant& value);

    QHash<QString, QVariant> m_values;
    QJsonObject m_loadedRoot;
};

} // namespace Hephaestus::Tools
