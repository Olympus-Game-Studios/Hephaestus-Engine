#include "AssetPathPolicy.h"
#include "AssetMountRegistry.h"

#include <algorithm>
#include <cctype>

namespace Hephaestus {
namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool StartsWithInsensitive(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool ContainsParentSegment(const std::string& relative) {
    if (relative == "..") {
        return true;
    }
    if (relative.rfind("../", 0) == 0 || relative.find("/../") != std::string::npos) {
        return true;
    }
    return relative.size() >= 3 && relative.compare(relative.size() - 3, 3, "/..") == 0;
}

std::string RemapViaMarkers(const std::string& generic) {
    for (const auto& marker : AssetPathPolicy::ProjectMarkers()) {
        const auto pos = generic.rfind(marker);
        if (pos != std::string::npos) {
            // marker includes leading '/', strip it: "/assets/" -> "assets/"
            return generic.substr(pos + 1);
        }
    }
    return {};
}

bool LooksLikeBareFilename(const std::string& path) {
    if (path.empty() || path.find('/') != std::string::npos || path.find('\\') != std::string::npos) {
        return false;
    }
    // Avoid treating "assets" directory names alone as filenames with extension requirement.
    return path.find('.') != std::string::npos;
}

} // namespace

const std::vector<std::string>& AssetPathPolicy::ProjectMarkers() {
    static const std::vector<std::string> markers = {
        "/assets/",
        "/.engine/",
        "/scripts/",
    };
    return markers;
}

std::string AssetPathPolicy::NormalizeSeparators(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::filesystem::path p(path);
    return p.lexically_normal().generic_string();
}

bool AssetPathPolicy::IsPortableKey(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path.rfind("builtin://", 0) == 0 || path.rfind("plugin://", 0) == 0 ||
        path.rfind("/plugin/", 0) == 0) {
        return true;
    }
    const std::string normalized = NormalizeSeparators(path);
    return StartsWithInsensitive(normalized, "assets/") ||
           StartsWithInsensitive(normalized, ".engine/") ||
           StartsWithInsensitive(normalized, "scripts/");
}

bool AssetPathPolicy::KeysEqual(const std::string& a, const std::string& b) {
    return ToLowerCopy(NormalizeSeparators(a)) == ToLowerCopy(NormalizeSeparators(b));
}

std::string AssetPathPolicy::ToPortableKey(const std::filesystem::path& projectRoot,
                                           const std::string& path) {
    if (path.empty()) {
        return {};
    }
    if (path.rfind("builtin://", 0) == 0 || path.rfind("plugin://", 0) == 0 ||
        path.rfind("/plugin/", 0) == 0) {
        return path;
    }

    const std::string normalized = NormalizeSeparators(path);
    if (IsPortableKey(normalized)) {
        return normalized;
    }

    // Marker remap works for absolute and UNC-style paths without a project root.
    if (const std::string viaMarker = RemapViaMarkers(normalized); !viaMarker.empty()) {
        return viaMarker;
    }

    std::filesystem::path input(path);
    if (!projectRoot.empty() && input.is_absolute()) {
        std::error_code ec;
        const std::filesystem::path canonPath = std::filesystem::weakly_canonical(input, ec);
        const std::filesystem::path root = projectRoot.lexically_normal();
        if (!ec) {
            const std::filesystem::path rel = canonPath.lexically_relative(root);
            const std::string relStr = rel.generic_string();
            if (!rel.empty() && !relStr.empty() && !ContainsParentSegment(relStr)) {
                return relStr;
            }
        } else {
            const std::filesystem::path rel = std::filesystem::relative(input.lexically_normal(), root, ec);
            if (!ec) {
                const std::string relStr = rel.generic_string();
                if (!rel.empty() && !relStr.empty() && !ContainsParentSegment(relStr)) {
                    return relStr;
                }
            }
        }
        // Absolute outside project — keep absolute (non-portable provenance).
        return normalized;
    }

    if (!input.is_absolute()) {
        return normalized;
    }

    // Absolute without project root and without markers: keep absolute.
    return normalized;
}

std::filesystem::path AssetPathPolicy::ResolvePortableKey(const std::filesystem::path& projectRoot,
                                                         const std::string& key) {
    if (key.empty() || key.rfind("builtin://", 0) == 0) {
        return {};
    }
    if (const auto mounted = AssetMountRegistry::Instance().Resolve(key)) {
        return *mounted;
    }

    std::filesystem::path p(key);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }

    if (projectRoot.empty()) {
        return p.lexically_normal();
    }

    return (projectRoot / p).lexically_normal();
}

AssetPathClassification AssetPathPolicy::Classify(const std::filesystem::path& projectRoot,
                                                  const std::string& path) {
    AssetPathClassification result;
    result.normalized = NormalizeSeparators(path);

    if (path.empty()) {
        result.kind = AssetPathKind::Empty;
        result.isPortable = true;
        return result;
    }

    if (path.rfind("builtin://", 0) == 0) {
        result.kind = AssetPathKind::Builtin;
        result.portableKey = path;
        result.isPortable = true;
        return result;
    }

    if (IsPortableKey(result.normalized)) {
        result.kind = AssetPathKind::PortableProjectRelative;
        result.portableKey = result.normalized;
        result.isPortable = true;
        return result;
    }

    std::filesystem::path input(path);
    if (!input.is_absolute() && LooksLikeBareFilename(result.normalized)) {
        result.kind = AssetPathKind::LegacyFilename;
        result.portableKey = result.normalized;
        result.isPortable = false;
        return result;
    }

    const std::string portable = ToPortableKey(projectRoot, path);
    if (IsPortableKey(portable)) {
        result.kind = input.is_absolute() ? AssetPathKind::AbsoluteInProject
                                          : AssetPathKind::PortableProjectRelative;
        result.portableKey = portable;
        result.isPortable = (result.kind == AssetPathKind::PortableProjectRelative);
        // AbsoluteInProject is not yet portable until rewritten, but has a portable mapping.
        if (result.kind == AssetPathKind::AbsoluteInProject) {
            result.isPortable = false;
        }
        return result;
    }

    if (input.is_absolute()) {
        result.kind = AssetPathKind::AbsoluteExternal;
        result.portableKey = portable;
        result.isPortable = false;
        return result;
    }

    if (!input.is_absolute()) {
        result.kind = AssetPathKind::PortableProjectRelative;
        result.portableKey = portable.empty() ? result.normalized : portable;
        result.isPortable = true;
        return result;
    }

    result.kind = AssetPathKind::Unknown;
    result.portableKey = portable;
    result.isPortable = false;
    return result;
}

} // namespace Hephaestus
