#include "VersionInfo.h"

namespace Hephaestus::Tools::VersionInfo {

QString EngineVersion()
{
    return QStringLiteral("1.0.0");
}

QString LauncherDisplayVersion()
{
    return QStringLiteral("Hephaestus Launcher ") + EngineVersion();
}

} // namespace Hephaestus::Tools::VersionInfo
