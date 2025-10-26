#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace MaterialEditor {

/**
 * Discovers all texture files in the project
 * @param projectRoot The root directory of the project
 * @return Vector of texture file paths relative to projectRoot
 */
std::vector<std::string> DiscoverProjectTextures(const std::filesystem::path& projectRoot);

/**
 * Gets the relative path for a texture file
 * @param projectRoot The root directory of the project
 * @param fullPath The full absolute path to the texture
 * @return Relative path from projectRoot
 */
std::string GetRelativeTexturePath(const std::filesystem::path& projectRoot, const std::filesystem::path& fullPath);

} // namespace MaterialEditor
