#pragma once

#include "PluginAPI/PluginAPI.h"

#include <QObject>
#include <QString>

#include <string>

/// File-name safe version of an asset's display name, used for its import folder.
inline std::string SafeFileName(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    while (!name.empty() && name.front() == ' ') {
        name.erase(name.begin());
    }
    return name.empty() ? std::string("Asset") : name;
}

/// One line saying what an asset pack import made, followed by its warnings.
inline QString ImportSummary(const Hephaestus::Plugins::AssetPackImportResult& result) {
    QString text = QObject::tr("Imported into %1: %n prefab(s)", nullptr, static_cast<int>(result.prefabCount))
                       .arg(QString::fromUtf8(result.folder));
    text += QObject::tr(", %n material(s).", nullptr, static_cast<int>(result.materialCount));
    if (result.warningCount > 0) {
        text += "\n" + QString::fromUtf8(result.message);
    }
    return text;
}
