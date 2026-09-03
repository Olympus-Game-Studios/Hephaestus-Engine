#if HEPHAESTUS_EDITOR_QT_UI

#include "InspectorMime.h"
#include "AddActorsPanel.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Assets/AssetTypes.h"

#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

AssetType AssetTypeFromInspectorKind(int kind)
{
    switch (static_cast<SpawnableEntry::Kind>(kind)) {
        case SpawnableEntry::Kind::Mesh: return AssetType::StaticMesh;
        case SpawnableEntry::Kind::Prefab: return AssetType::Prefab;
        case SpawnableEntry::Kind::Audio: return AssetType::Audio;
        case SpawnableEntry::Kind::Material: return AssetType::Material;
        case SpawnableEntry::Kind::Script: return AssetType::Script;
        case SpawnableEntry::Kind::Texture: return AssetType::Texture;
        case SpawnableEntry::Kind::UILayout: return AssetType::UILayout;
        case SpawnableEntry::Kind::Character: return AssetType::Character;
        default: return AssetType::Unknown;
    }
}

bool AssetTypeCompatible(AssetType expected, AssetType actual)
{
    if (expected == AssetType::Unknown || actual == AssetType::Unknown) {
        return expected == AssetType::Unknown;
    }
    return expected == actual;
}

static QString ToPortable(const QString& raw)
{
    if (raw.isEmpty()) return {};
    auto& assets = AssetManager::GetInstance();
    const std::filesystem::path projectRoot = assets.GetProjectRoot();
    std::string portable = AssetPathPolicy::ToPortableKey(projectRoot, raw.toStdString());
    if (portable.empty()) {
        portable = raw.toStdString();
    }
    return QString::fromStdString(portable);
}

bool ExtractInspectorAssetDrop(const QMimeData* mime, InspectorAssetDrop& out)
{
    if (!mime) return false;
    if (mime->hasFormat(kSpawnableMimeType)) {
        const SpawnableEntry entry = SpawnableEntry::fromMimePayload(mime->data(kSpawnableMimeType));
        if (!entry.assetPath.isEmpty()) {
            out.portablePath = ToPortable(entry.assetPath);
            out.type = AssetTypeFromInspectorKind(static_cast<int>(entry.kind));
            if (out.type == AssetType::Unknown) {
                out.type = AssetTypeFromExtension(
                    std::filesystem::path(out.portablePath.toStdString()).extension().string());
            }
            return !out.portablePath.isEmpty();
        }
    }
    if (mime->hasUrls() && !mime->urls().isEmpty()) {
        const QString local = mime->urls().front().toLocalFile();
        if (!local.isEmpty()) {
            out.portablePath = ToPortable(local);
            out.type = AssetTypeFromExtension(
                std::filesystem::path(out.portablePath.toStdString()).extension().string());
            return !out.portablePath.isEmpty();
        }
    }
    if (mime->hasText()) {
        const QString text = mime->text().trimmed();
        if (!text.isEmpty()) {
            out.portablePath = ToPortable(text);
            out.type = AssetTypeFromExtension(
                std::filesystem::path(out.portablePath.toStdString()).extension().string());
            return !out.portablePath.isEmpty();
        }
    }
    return false;
}

void AttachInspectorAssetMime(QMimeData* mime, const QString& portablePath, AssetType type)
{
    if (!mime) return;
    SpawnableEntry entry;
    entry.assetPath = portablePath;
    entry.displayName = QFileInfo(portablePath).completeBaseName();
    switch (type) {
        case AssetType::Prefab: entry.kind = SpawnableEntry::Kind::Prefab; break;
        case AssetType::Audio: entry.kind = SpawnableEntry::Kind::Audio; break;
        case AssetType::Material: entry.kind = SpawnableEntry::Kind::Material; break;
        case AssetType::Script: entry.kind = SpawnableEntry::Kind::Script; break;
        case AssetType::Texture: entry.kind = SpawnableEntry::Kind::Texture; break;
        case AssetType::UILayout: entry.kind = SpawnableEntry::Kind::UILayout; break;
        case AssetType::Character: entry.kind = SpawnableEntry::Kind::Character; break;
        default: entry.kind = SpawnableEntry::Kind::Mesh; break;
    }
    mime->setData(kSpawnableMimeType, entry.toMimePayload());
    mime->setText(portablePath);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
