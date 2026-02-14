#include "JsonUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace Hephaestus::Tools::JsonUtils {

bool ReadJsonObject(const QString& filePath, QJsonObject* outObject)
{
    if (!outObject) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    *outObject = doc.object();
    return true;
}

bool WriteJsonObject(const QString& filePath, const QJsonObject& object)
{
    const QFileInfo info(filePath);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const QJsonDocument doc(object);
    const qint64 written = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return written >= 0;
}

} // namespace Hephaestus::Tools::JsonUtils
