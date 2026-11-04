#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetThumbnailCache.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetThumbnailRenderer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStringList>

#include <exception>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

namespace {

constexpr int kThumbnailSize = 256;
// Bump when the renderer's output changes so old cache files are regenerated.
const QString kCacheVersion = QStringLiteral("1");

QString Stamp(const QString& path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1\t%2\t%3").arg(path).arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
}

/** The cached image, if it was made by this version from files that have not changed since. */
QImage LoadValidCache(const QString& cacheFile)
{
    QImageReader reader(cacheFile);
    if (!reader.canRead() || reader.text(QStringLiteral("version")) != kCacheVersion) {
        return {};
    }
    const QStringList dependencies = reader.text(QStringLiteral("dependencies")).split('\n', Qt::SkipEmptyParts);
    if (dependencies.isEmpty()) {
        return {};
    }
    for (const QString& stamp : dependencies) {
        if (Stamp(stamp.section('\t', 0, 0)) != stamp) {
            return {};
        }
    }
    return reader.read();
}

QImage ToQImage(const ThumbnailImage& image)
{
    return QImage(image.rgba.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888).copy();
}

QImage Generate(const std::filesystem::path& projectRoot, const std::filesystem::path& assetRoot,
                const std::filesystem::path& file, AssetType type, QStringList& outDependencies)
{
    std::vector<std::filesystem::path> dependencies;
    QImage image;
    if (type == AssetType::Texture) {
        dependencies.push_back(file);
        ThumbnailImage texture;
        if (LoadThumbnailImage(file, texture)) {
            ShrinkThumbnailImage(texture, kThumbnailSize * 2);
            image = ToQImage(texture).scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    } else {
        ThumbnailImage render;
        std::string error;
        if (RenderAssetThumbnail(projectRoot, assetRoot, file, type, kThumbnailSize, render, dependencies, error)) {
            image = ToQImage(render);
        }
    }
    for (const auto& dependency : dependencies) {
        outDependencies << Stamp(QString::fromStdString(dependency.generic_string()));
    }
    return image;
}

} // namespace

AssetThumbnailCache::AssetThumbnailCache(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
    m_pool.setThreadPriority(QThread::LowPriority);
}

AssetThumbnailCache::~AssetThumbnailCache()
{
    m_pool.clear();
    m_pool.waitForDone();
}

bool AssetThumbnailCache::Supports(AssetType type)
{
    return type == AssetType::Texture || type == AssetType::Material ||
           type == AssetType::StaticMesh || type == AssetType::Prefab;
}

QPixmap AssetThumbnailCache::Thumbnail(const QString& path, AssetType type)
{
    const QString relativePath = QDir::fromNativeSeparators(path);
    if (!Supports(type)) {
        return {};
    }
    auto& assets = AssetManager::GetInstance();
    const QString projectRoot = QString::fromStdString(assets.GetProjectRoot());
    if (projectRoot != m_projectRoot) {
        CancelPending();
        m_entries.clear();
        m_projectRoot = projectRoot;
        m_projectGeneration = ++m_generation;
    }

    const Entry entry = m_entries.value(relativePath);
    if (entry.generation == m_generation || m_pending.contains(relativePath) || projectRoot.isEmpty()) {
        return entry.pixmap;
    }
    m_pending.insert(relativePath);

    const std::filesystem::path assetRoot = std::filesystem::u8path(assets.GetAssetRoot());
    const std::filesystem::path file = (assetRoot / std::filesystem::u8path(relativePath.toStdString())).lexically_normal();
    const QByteArray key = QCryptographicHash::hash(relativePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString cacheFile = QDir(projectRoot).filePath(QStringLiteral(".hephaestus/thumbnails/%1.png").arg(QString::fromLatin1(key)));
    const std::filesystem::path root = std::filesystem::u8path(projectRoot.toStdString());
    const quint64 generation = m_generation;

    m_pool.start(QRunnable::create([this, relativePath, type, file, root, assetRoot, cacheFile, generation]() {
        QImage image = LoadValidCache(cacheFile);
        if (image.isNull()) {
            QStringList dependencies;
            try {
                image = Generate(root, assetRoot, file, type, dependencies);
            } catch (const std::exception& e) {
                std::cerr << "[Thumbnails] " << relativePath.toStdString() << ": " << e.what() << "\n";
            }
            if (!image.isNull()) {
                image.setText(QStringLiteral("version"), kCacheVersion);
                image.setText(QStringLiteral("dependencies"), dependencies.join('\n'));
                QDir().mkpath(QFileInfo(cacheFile).absolutePath());
                QSaveFile out(cacheFile);
                if (out.open(QIODevice::WriteOnly) && image.save(&out, "PNG")) {
                    out.commit();
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, relativePath, generation, image]() {
            onFinished(relativePath, generation, image);
        }, Qt::QueuedConnection);
    }));
    return entry.pixmap;
}

void AssetThumbnailCache::Invalidate()
{
    ++m_generation;
}

void AssetThumbnailCache::CancelPending()
{
    m_pool.clear();
    m_pending.clear();
}

void AssetThumbnailCache::onFinished(const QString& relativePath, quint64 generation, const QImage& image)
{
    if (generation < m_projectGeneration) {
        return;
    }
    m_pending.remove(relativePath);
    Entry& entry = m_entries[relativePath];
    entry.pixmap = QPixmap::fromImage(image);
    entry.generation = generation;
    emit thumbnailReady(relativePath);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
