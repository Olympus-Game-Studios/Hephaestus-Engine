#pragma once

#include <QJsonObject>
#include <QString>

namespace Hephaestus::Tools::JsonUtils {

bool ReadJsonObject(const QString& filePath, QJsonObject* outObject);
bool WriteJsonObject(const QString& filePath, const QJsonObject& object);

} // namespace Hephaestus::Tools::JsonUtils
