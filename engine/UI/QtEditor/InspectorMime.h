#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Assets/AssetTypes.h"

#include <QString>

class QMimeData;

namespace Hephaestus {
namespace QtEditor {

struct InspectorAssetDrop {
    QString portablePath;
    AssetType type = AssetType::Unknown;
};

bool ExtractInspectorAssetDrop(const QMimeData* mime, InspectorAssetDrop& out);
void AttachInspectorAssetMime(QMimeData* mime, const QString& portablePath, AssetType type);
AssetType AssetTypeFromInspectorKind(int kind);
bool AssetTypeCompatible(AssetType expected, AssetType actual);

} // namespace QtEditor
} // namespace Hephaestus

#endif
