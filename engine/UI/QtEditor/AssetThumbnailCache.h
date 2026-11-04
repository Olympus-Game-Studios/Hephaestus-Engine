/**
 * @file AssetThumbnailCache.h
 * @brief Background-generated, disk-cached Content Browser thumbnails.
 */

#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Assets/AssetTypes.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QThreadPool>

namespace Hephaestus {
namespace QtEditor {

/**
 * Textures show the image itself; materials, meshes and prefabs show a preview render.
 * Renders run on a low-priority pool and are kept under <project>/.hephaestus/thumbnails.
 */
class AssetThumbnailCache : public QObject
{
    Q_OBJECT

public:
    explicit AssetThumbnailCache(QObject* parent = nullptr);
    ~AssetThumbnailCache() override;

    static bool Supports(AssetType type);

    /**
     * The current thumbnail, or the previous one while it is being revalidated; null until the
     * first one is ready. Queues generation as needed; thumbnailReady follows.
     */
    QPixmap Thumbnail(const QString& relativePath, AssetType type);

    /** Rechecks every thumbnail against its source files on the next request. */
    void Invalidate();

    /** Drops queued requests that have not started. */
    void CancelPending();

signals:
    /** relativePath uses forward slashes. */
    void thumbnailReady(const QString& relativePath);

private:
    struct Entry {
        QPixmap pixmap;
        quint64 generation = 0;
    };

    void onFinished(const QString& relativePath, quint64 generation, const QImage& image);

    QThreadPool m_pool;
    QString m_projectRoot;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_pending;
    quint64 m_generation = 1;
    quint64 m_projectGeneration = 1;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
