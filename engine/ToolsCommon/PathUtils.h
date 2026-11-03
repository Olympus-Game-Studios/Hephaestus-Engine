#pragma once

#include <QString>

namespace Hephaestus::Tools::PathUtils {

// Engine folder: assets, shaders, Plugins and sibling tools (see ResourceRootFor).
QString ExecutableDir();
QString SiblingExecutablePath(const QString& executableBaseName);

} // namespace Hephaestus::Tools::PathUtils
