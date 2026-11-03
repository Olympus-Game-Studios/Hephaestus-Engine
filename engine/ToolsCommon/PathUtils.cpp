#include "PathUtils.h"

#include "Core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace Hephaestus::Tools::PathUtils {

QString ExecutableDir()
{
    const auto root = ResourceRootFor(QCoreApplication::applicationDirPath().toStdWString());
    return QDir::cleanPath(QString::fromStdWString(root.wstring()));
}

QString SiblingExecutablePath(const QString& executableBaseName)
{
    QString fileName = executableBaseName;
#if defined(Q_OS_WIN)
    if (!fileName.endsWith(".exe", Qt::CaseInsensitive)) {
        fileName += ".exe";
    }
#elif defined(Q_OS_MACOS)
    // A release ships the editor beside the launcher in one .app; the build tree has separate apps.
    const QString sameBundle = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
    if (QFileInfo::exists(sameBundle)) {
        return sameBundle;
    }
    const QString bundled = QDir(ExecutableDir()).filePath(
        fileName + QStringLiteral(".app/Contents/MacOS/") + fileName);
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }
#endif
    return QDir(ExecutableDir()).filePath(fileName);
}

} // namespace Hephaestus::Tools::PathUtils
