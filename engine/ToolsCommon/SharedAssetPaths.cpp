#include "SharedAssetPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace Hephaestus::Tools {
namespace {

static QString FirstExistingDir(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isDir()) {
            return QDir::cleanPath(info.absoluteFilePath());
        }
    }
    return {};
}

} // namespace

SharedAssetPaths SharedAssetPaths::Locate()
{
    SharedAssetPaths paths;

    const QString binDir = QDir::cleanPath(QCoreApplication::applicationDirPath());

    const QString envOverride = qEnvironmentVariable("HEPHAESTUS_ASSETS_DIR");
    if (!envOverride.isEmpty()) {
        const QFileInfo envInfo(envOverride);
        if (envInfo.exists() && envInfo.isDir()) {
            paths.root = QDir::cleanPath(envInfo.absoluteFilePath());
        }
    }

    if (paths.root.isEmpty()) {
        const QStringList requiredOrderCandidates = {
            QDir(binDir).filePath("Assets"),
            QDir(binDir).filePath("EditorAssets"),
            QDir(binDir).filePath("../Assets"),
            QDir(binDir).filePath("../EditorAssets"),
            QDir(binDir).filePath("../share/hephaestus")
        };

        paths.root = FirstExistingDir(requiredOrderCandidates);
    }

    if (paths.root.isEmpty()) {
        const QStringList compatibilityCandidates = {
            QDir(binDir).filePath("assets"),
            QDir(binDir).filePath("editor"),
            QDir(binDir).filePath("../assets"),
            QDir(binDir).filePath("../editor")
        };
        paths.root = FirstExistingDir(compatibilityCandidates);
    }

    if (paths.root.isEmpty()) {
        return paths;
    }

    const QDir rootDir(paths.root);

    paths.iconsDir = FirstExistingDir({
        rootDir.filePath("icons"),
        rootDir.filePath("Icons")
    });

    paths.imagesDir = FirstExistingDir({
        rootDir.filePath("images"),
        rootDir.filePath("Images"),
        rootDir.filePath("assets/ui")
    });

    paths.fontsDir = FirstExistingDir({
        rootDir.filePath("fonts"),
        rootDir.filePath("Fonts")
    });

    paths.stylesDir = FirstExistingDir({
        rootDir.filePath("styles"),
        rootDir.filePath("Styles"),
        rootDir.filePath("assets/styles")
    });

    paths.i18nDir = FirstExistingDir({
        rootDir.filePath("i18n"),
        rootDir.filePath("translations")
    });

    paths.thumbnailsDir = FirstExistingDir({
        rootDir.filePath("thumbnails"),
        rootDir.filePath("assets/thumbnails")
    });

    return paths;
}

QString SharedAssetPaths::Resolve(const QString& relative) const
{
    if (!IsValid()) {
        return {};
    }

    if (QDir::isAbsolutePath(relative)) {
        return QDir::cleanPath(relative);
    }

    return QDir::cleanPath(QDir(root).filePath(relative));
}

bool SharedAssetPaths::IsValid() const
{
    const QFileInfo info(root);
    return !root.isEmpty() && info.exists() && info.isDir();
}

} // namespace Hephaestus::Tools
