#pragma once

#include <QString>

namespace Hephaestus::Tools::PathUtils {

QString ExecutableDir();
QString SiblingExecutablePath(const QString& executableBaseName);

} // namespace Hephaestus::Tools::PathUtils
