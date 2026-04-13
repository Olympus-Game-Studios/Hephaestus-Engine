#include "VersionInfo.h"
#include "AppConfig.h"

namespace Hephaestus::Tools::VersionInfo {

QString EngineVersion()     { return QStringLiteral(HEPHAESTUS_VERSION_STRING); }
QString LauncherDisplayVersion() { return QStringLiteral("Hephaestus Launcher ") + EngineVersion(); }
QString AppName()           { return QStringLiteral(HEPHAESTUS_APP_NAME); }
QString AppDescription()    { return QStringLiteral(HEPHAESTUS_APP_DESCRIPTION); }
QString Organization()      { return QStringLiteral(HEPHAESTUS_ORGANIZATION); }
QString Domain()            { return QStringLiteral(HEPHAESTUS_DOMAIN); }
QString Website()           { return QStringLiteral(HEPHAESTUS_WEBSITE); }
QString Copyright()         { return QStringLiteral(HEPHAESTUS_COPYRIGHT); }

} // namespace Hephaestus::Tools::VersionInfo
