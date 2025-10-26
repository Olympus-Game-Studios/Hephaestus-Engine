#include "TextureDiscovery.h"

#include <algorithm>
#include <cctype>

namespace MaterialEditor {

static const std::vector<std::string> TEXTURE_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr", ".dds"
};

static bool IsTextureFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& ext : TEXTURE_EXTENSIONS) {
        if (lower.length() >= ext.length() &&
            lower.compare(lower.length() - ext.length(), ext.length(), ext) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> DiscoverProjectTextures(const std::filesystem::path& projectRoot) {
    std::vector<std::string> textures;

    try {
        // Common texture directories to search
        std::vector<std::filesystem::path> searchDirs = {
            projectRoot / "assets" / "textures",
            projectRoot / "assets",
            projectRoot / "textures",
            projectRoot / "Textures",
        };

        for (const auto& dir : searchDirs) {
            if (!std::filesystem::exists(dir)) {
                continue;
            }

            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }

                    if (!IsTextureFile(entry.path().filename().string())) {
                        continue;
                    }

                    // Get relative path from project root
                    std::filesystem::path relPath = std::filesystem::relative(entry.path(), projectRoot);
                    textures.push_back(relPath.generic_string());
                }
            } catch (const std::filesystem::filesystem_error&) {
                // Skip directories that can't be accessed
                continue;
            }
        }

        // Sort and remove duplicates
        std::sort(textures.begin(), textures.end());
        textures.erase(std::unique(textures.begin(), textures.end()), textures.end());

    } catch (const std::exception&) {
        // Return empty list if discovery fails
    }

    return textures;
}

std::string GetRelativeTexturePath(const std::filesystem::path& projectRoot, const std::filesystem::path& fullPath) {
    try {
        std::filesystem::path relPath = std::filesystem::relative(fullPath, projectRoot);
        return relPath.generic_string();
    } catch (const std::filesystem::filesystem_error&) {
        return fullPath.generic_string();
    }
}

} // namespace MaterialEditor
