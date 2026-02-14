#pragma once

#include "LauncherProject.h"

#include <QObject>
#include <QFutureWatcher>
#include <QVector>

namespace Hephaestus::Launcher {

class LauncherProjectScanner : public QObject {
    Q_OBJECT

public:
    explicit LauncherProjectScanner(QObject* parent = nullptr);
    ~LauncherProjectScanner() override;

    void StartScan(const QStringList& rootFolders);
    bool IsScanning() const;

signals:
    void ScanStarted();
    void ScanFinished(const QVector<LauncherProject>& projects);

private:
    static QVector<LauncherProject> ScanWorker(const QStringList& rootFolders);

private:
    QFutureWatcher<QVector<LauncherProject>> m_watcher;
};

} // namespace Hephaestus::Launcher
