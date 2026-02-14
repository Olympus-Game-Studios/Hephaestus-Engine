#include "PathUtils.h"

#include <QCoreApplication>
#include <QDir>

namespace Hephaestus::Tools::PathUtils {

QString ExecutableDir()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath());
}

QString SiblingExecutablePath(const QString& executableBaseName)
{
    QString fileName = executableBaseName;
#if defined(Q_OS_WIN)
    if (!fileName.endsWith(".exe", Qt::CaseInsensitive)) {
        fileName += ".exe";
    }
#endif
    return QDir(ExecutableDir()).filePath(fileName);
}

} // namespace Hephaestus::Tools::PathUtils
