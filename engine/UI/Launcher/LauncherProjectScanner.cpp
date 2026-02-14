#include "LauncherProjectScanner.h"

#include "LauncherProjectManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QSet>

namespace Hephaestus::Launcher {

LauncherProjectScanner::LauncherProjectScanner(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QVector<LauncherProject>>::finished, this, [this]() {
        emit ScanFinished(m_watcher.result());
    });
}

LauncherProjectScanner::~LauncherProjectScanner()
{
    if (m_watcher.isRunning()) {
        m_watcher.cancel();
        m_watcher.waitForFinished();
    }
}

void LauncherProjectScanner::StartScan(const QStringList& rootFolders)
{
    if (m_watcher.isRunning()) {
        m_watcher.cancel();
        m_watcher.waitForFinished();
    }

    emit ScanStarted();
    m_watcher.setFuture(QtConcurrent::run([rootFolders]() {
        return ScanWorker(rootFolders);
    }));
}

bool LauncherProjectScanner::IsScanning() const
{
    return m_watcher.isRunning();
}

QVector<LauncherProject> LauncherProjectScanner::ScanWorker(const QStringList& rootFolders)
{
    QVector<LauncherProject> projects;
    QSet<QString> uniqueProjectDirs;

    LauncherProjectManager manager;

    for (const QString& root : rootFolders) {
        QFileInfo rootInfo(root);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            continue;
        }

        QDirIterator it(root,
                        QStringList{QStringLiteral("project.heproject")},
                        QDir::Files,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString projectFilePath = it.next();
            LauncherProject project;
            if (manager.ReadProject(projectFilePath, &project)) {
                const QString uniqueKey = QFileInfo(project.projectDir).canonicalFilePath().isEmpty()
                    ? QDir::cleanPath(project.projectDir)
                    : QFileInfo(project.projectDir).canonicalFilePath();
                if (!uniqueProjectDirs.contains(uniqueKey)) {
                    uniqueProjectDirs.insert(uniqueKey);
                    projects.push_back(project);
                }
                continue;
            }

            LauncherProject broken;
            QFileInfo fileInfo(projectFilePath);
            broken.projectFilePath = fileInfo.absoluteFilePath();
            if (fileInfo.dir().dirName().compare(QStringLiteral(".hephaestus"), Qt::CaseInsensitive) == 0) {
                QDir dir = fileInfo.dir();
                dir.cdUp();
                broken.projectDir = dir.absolutePath();
            } else {
                broken.projectDir = fileInfo.dir().absolutePath();
            }
            broken.name = QFileInfo(broken.projectDir).fileName();
            broken.engineVersion = QStringLiteral("Unknown");
            broken.broken = true;
            broken.brokenReason = QStringLiteral("Invalid or unreadable project metadata");
            broken.lastModified = fileInfo.lastModified();

            const QString uniqueKey = QFileInfo(broken.projectDir).canonicalFilePath().isEmpty()
                ? QDir::cleanPath(broken.projectDir)
                : QFileInfo(broken.projectDir).canonicalFilePath();

            if (!uniqueProjectDirs.contains(uniqueKey)) {
                uniqueProjectDirs.insert(uniqueKey);
                projects.push_back(broken);
            }
        }
    }

    return projects;
}

} // namespace Hephaestus::Launcher
