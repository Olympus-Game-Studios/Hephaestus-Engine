#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Hephaestus::Launcher {

struct LauncherProject {
    QString projectDir;
    QString projectFilePath;

    QString name;
    QString description;
    QString engineVersion;
    QString editorMode;
    QString screenshotPath;
    QString screenshotAbsolutePath;
    QStringList tags;

    bool favorite = false;
    bool broken = false;
    QString brokenReason;

    QDateTime created;
    QDateTime lastModified;
};

} // namespace Hephaestus::Launcher
