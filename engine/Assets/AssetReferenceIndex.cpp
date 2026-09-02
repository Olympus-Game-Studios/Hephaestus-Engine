#include "AssetReferenceIndex.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Hephaestus {
namespace {

using json = nlohmann::json;

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string ExtensionLower(const std::filesystem::path& path) {
    return ToLowerCopy(path.extension().string());
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

bool IsIndexableExtension(const std::string& ext) {
    return ext == ".hpscene" || ext == ".hpprefab" || ext == ".hematerial" ||
           ext == ".hui" || ext == ".heproject" || ext == ".json";
}

bool LooksLikeAssetPath(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    if (AssetPathPolicy::IsPortableKey(value)) {
        return true;
    }
    if (value.rfind("builtin://", 0) == 0) {
        return true;
    }
    const std::string normalized = AssetPathPolicy::NormalizeSeparators(value);
    return StartsWithInsensitive(normalized, "assets/") ||
           StartsWithInsensitive(normalized, ".engine/") ||
           StartsWithInsensitive(normalized, "scripts/");
}

bool LooksLikeControllerPath(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    if (LooksLikeAssetPath(value)) {
        return true;
    }
    const std::string lower = ToLowerCopy(value);
    return value.find('/') != std::string::npos || value.find('\\') != std::string::npos ||
           (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".lua") == 0);
}

AssetReferenceFieldKind FieldKindFromKey(const std::string& key) {
    if (key == "meshSource") {
        return AssetReferenceFieldKind::MeshSource;
    }
    if (key == "appliedMaterialPaths") {
        return AssetReferenceFieldKind::AppliedMaterialPath;
    }
    if (key == "albedoTexturePath" || key == "normalTexturePath" ||
        key == "roughnessTexturePath" || key == "metallicTexturePath" ||
        key == "aoTexturePath") {
        return AssetReferenceFieldKind::TexturePath;
    }
    if (key == "scriptPath") {
        return AssetReferenceFieldKind::ScriptPath;
    }
    if (key == "clipPath") {
        return AssetReferenceFieldKind::ClipPath;
    }
    if (key == "hdriPath" || key == "hdrPath" || key == "environmentPath" ||
        key == "skyboxPath" || key == "skybox") {
        return AssetReferenceFieldKind::HdriPath;
    }
    if (key == "prefabPath" || key == "blueprintPath") {
        return AssetReferenceFieldKind::PrefabPath;
    }
    if (key == "resourcePath") {
        return AssetReferenceFieldKind::ResourcePath;
    }
    if (key == "imageId") {
        return AssetReferenceFieldKind::ImageId;
    }
    if (key == "backgroundImageId") {
        return AssetReferenceFieldKind::BackgroundImageId;
    }
    if (key == "controller") {
        return AssetReferenceFieldKind::Controller;
    }
    if (key == "defaultScene") {
        return AssetReferenceFieldKind::DefaultScene;
    }
    if (key == "lastScene") {
        return AssetReferenceFieldKind::LastScene;
    }
    if (key == "defaultInputProfile") {
        return AssetReferenceFieldKind::DefaultInputProfile;
    }
    return AssetReferenceFieldKind::Unknown;
}

bool IsKnownPathKey(const std::string& key) {
    return FieldKindFromKey(key) != AssetReferenceFieldKind::Unknown;
}

bool AllowKeyForExtension(const std::string& ext, const std::string& key) {
    const AssetReferenceFieldKind kind = FieldKindFromKey(key);
    if (kind == AssetReferenceFieldKind::Unknown) {
        return false;
    }

    if (ext == ".hpscene") {
        return kind == AssetReferenceFieldKind::MeshSource ||
               kind == AssetReferenceFieldKind::AppliedMaterialPath ||
               kind == AssetReferenceFieldKind::TexturePath ||
               kind == AssetReferenceFieldKind::ScriptPath ||
               kind == AssetReferenceFieldKind::ClipPath ||
               kind == AssetReferenceFieldKind::HdriPath ||
               kind == AssetReferenceFieldKind::PrefabPath;
    }
    if (ext == ".hpprefab") {
        return kind == AssetReferenceFieldKind::MeshSource ||
               kind == AssetReferenceFieldKind::AppliedMaterialPath ||
               kind == AssetReferenceFieldKind::TexturePath ||
               kind == AssetReferenceFieldKind::ScriptPath ||
               kind == AssetReferenceFieldKind::ClipPath ||
               kind == AssetReferenceFieldKind::HdriPath ||
               kind == AssetReferenceFieldKind::PrefabPath ||
               kind == AssetReferenceFieldKind::ResourcePath;
    }
    if (ext == ".hematerial") {
        return kind == AssetReferenceFieldKind::TexturePath ||
               kind == AssetReferenceFieldKind::ResourcePath;
    }
    if (ext == ".hui") {
        return kind == AssetReferenceFieldKind::ImageId ||
               kind == AssetReferenceFieldKind::BackgroundImageId ||
               kind == AssetReferenceFieldKind::Controller;
    }
    if (ext == ".heproject" || ext == ".json") {
        return kind == AssetReferenceFieldKind::DefaultScene ||
               kind == AssetReferenceFieldKind::LastScene ||
               kind == AssetReferenceFieldKind::DefaultInputProfile;
    }
    return false;
}

std::string JoinLocation(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    return parent + "/" + child;
}

bool ReadFileText(const std::filesystem::path& path, std::string& out, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) {
            *errorOut = "Failed to open file: " + path.generic_string();
        }
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileText(const std::filesystem::path& path, const std::string& text, std::string* errorOut) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (errorOut) {
            *errorOut = "Failed to write file: " + path.generic_string();
        }
        return false;
    }
    out << text;
    if (!out) {
        if (errorOut) {
            *errorOut = "Failed while writing file: " + path.generic_string();
        }
        return false;
    }
    return true;
}

struct CollectContext {
    const std::filesystem::path* projectRoot = nullptr;
    std::string documentKey;
    std::string extension;
    bool allowAdvisoryNested = false;
    std::vector<AssetReference>* out = nullptr;
};

void EmitReference(CollectContext& ctx,
                   AssetReferenceFieldKind kind,
                   const std::string& location,
                   const std::string& rawPath,
                   bool advisory) {
    if (!ctx.out || rawPath.empty()) {
        return;
    }
    AssetReference ref;
    ref.documentPath = ctx.documentKey;
    ref.fieldKind = kind;
    ref.structuredLocation = location;
    ref.referencedPath = AssetPathPolicy::ToPortableKey(*ctx.projectRoot, rawPath);
    if (ref.referencedPath.empty()) {
        return;
    }
    ref.isAdvisory = advisory;
    ctx.out->push_back(std::move(ref));
}

void CollectKnownString(CollectContext& ctx,
                        const std::string& key,
                        const std::string& location,
                        const std::string& value) {
    if (!AllowKeyForExtension(ctx.extension, key) || value.empty()) {
        return;
    }
    if (key == "controller" && !LooksLikeControllerPath(value)) {
        return;
    }
    EmitReference(ctx, FieldKindFromKey(key), location, value, false);
}

void WalkCollect(CollectContext& ctx, const json& node, const std::string& location, const std::string& parentKey) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string key = it.key();
            const std::string childLoc = JoinLocation(location, key);
            const json& child = it.value();

            if (child.is_string()) {
                CollectKnownString(ctx, key, childLoc, child.get_ref<const std::string&>());
                if (ctx.allowAdvisoryNested && !IsKnownPathKey(key)) {
                    const std::string& value = child.get_ref<const std::string&>();
                    if (LooksLikeAssetPath(value)) {
                        EmitReference(ctx, AssetReferenceFieldKind::NestedAssetPath, childLoc, value, true);
                    }
                }
            } else if (child.is_array() && key == "appliedMaterialPaths" &&
                       AllowKeyForExtension(ctx.extension, key)) {
                for (size_t i = 0; i < child.size(); ++i) {
                    if (child[i].is_string()) {
                        const std::string idxLoc = childLoc + "/" + std::to_string(i);
                        EmitReference(ctx, AssetReferenceFieldKind::AppliedMaterialPath, idxLoc,
                                      child[i].get_ref<const std::string&>(), false);
                    }
                }
            } else {
                WalkCollect(ctx, child, childLoc, key);
            }
        }
        return;
    }

    if (node.is_array()) {
        for (size_t i = 0; i < node.size(); ++i) {
            WalkCollect(ctx, node[i], JoinLocation(location, std::to_string(i)), parentKey);
        }
        return;
    }

    if (node.is_string() && ctx.allowAdvisoryNested && !IsKnownPathKey(parentKey)) {
        const std::string& value = node.get_ref<const std::string&>();
        if (LooksLikeAssetPath(value)) {
            EmitReference(ctx, AssetReferenceFieldKind::NestedAssetPath, location, value, true);
        }
    }
}

int RewriteStringIfMatch(std::string& value,
                         const std::string& oldPath,
                         const std::string& newPath) {
    if (AssetPathPolicy::KeysEqual(value, oldPath)) {
        value = newPath;
        return 1;
    }
    return 0;
}

int WalkRewrite(json& node,
                const std::string& extension,
                bool allowAdvisoryNested,
                const std::string& parentKey,
                const std::string& oldPath,
                const std::string& newPath) {
    int count = 0;
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string key = it.key();
            json& child = it.value();
            if (child.is_string()) {
                std::string value = child.get<std::string>();
                bool shouldRewrite = false;
                if (AllowKeyForExtension(extension, key)) {
                    if (key == "controller") {
                        shouldRewrite = LooksLikeControllerPath(value);
                    } else {
                        shouldRewrite = true;
                    }
                } else if (allowAdvisoryNested && !IsKnownPathKey(key) && LooksLikeAssetPath(value)) {
                    shouldRewrite = true;
                }
                if (shouldRewrite) {
                    count += RewriteStringIfMatch(value, oldPath, newPath);
                    child = value;
                }
            } else if (child.is_array() && key == "appliedMaterialPaths" &&
                       AllowKeyForExtension(extension, key)) {
                for (auto& entry : child) {
                    if (entry.is_string()) {
                        std::string value = entry.get<std::string>();
                        count += RewriteStringIfMatch(value, oldPath, newPath);
                        entry = value;
                    }
                }
            } else {
                count += WalkRewrite(child, extension, allowAdvisoryNested, key, oldPath, newPath);
            }
        }
        return count;
    }

    if (node.is_array()) {
        for (auto& child : node) {
            count += WalkRewrite(child, extension, allowAdvisoryNested, parentKey, oldPath, newPath);
        }
        return count;
    }

    return count;
}

void CollectFallbackSceneKeys(CollectContext& ctx, const std::string& text) {
    static const char* kKeys[] = {
        "meshSource",
        "albedoTexturePath",
        "normalTexturePath",
        "roughnessTexturePath",
        "metallicTexturePath",
        "aoTexturePath",
        "scriptPath",
        "clipPath",
        "hdriPath",
        "hdrPath",
        "environmentPath",
        "skyboxPath",
        "prefabPath",
        "blueprintPath",
    };

    for (const char* key : kKeys) {
        if (!AllowKeyForExtension(".hpscene", key)) {
            continue;
        }
        const std::string needle = std::string("\"") + key + "\"";
        size_t pos = 0;
        int occurrence = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            size_t colon = text.find(':', pos + needle.size());
            if (colon == std::string::npos) {
                break;
            }
            size_t q1 = text.find('"', colon + 1);
            if (q1 == std::string::npos) {
                break;
            }
            size_t q2 = text.find('"', q1 + 1);
            if (q2 == std::string::npos) {
                break;
            }
            const std::string value = text.substr(q1 + 1, q2 - q1 - 1);
            const std::string location =
                std::string("fallback/") + key + "/" + std::to_string(occurrence);
            CollectKnownString(ctx, key, location, value);
            ++occurrence;
            pos = q2 + 1;
        }
    }

    // appliedMaterialPaths array entries: "assets/..."
    const std::string arrayKey = "\"appliedMaterialPaths\"";
    size_t pos = 0;
    int occurrence = 0;
    while ((pos = text.find(arrayKey, pos)) != std::string::npos) {
        size_t bracket = text.find('[', pos + arrayKey.size());
        if (bracket == std::string::npos) {
            break;
        }
        size_t end = text.find(']', bracket + 1);
        if (end == std::string::npos) {
            break;
        }
        size_t cursor = bracket + 1;
        int index = 0;
        while (cursor < end) {
            size_t q1 = text.find('"', cursor);
            if (q1 == std::string::npos || q1 >= end) {
                break;
            }
            size_t q2 = text.find('"', q1 + 1);
            if (q2 == std::string::npos || q2 >= end) {
                break;
            }
            const std::string value = text.substr(q1 + 1, q2 - q1 - 1);
            const std::string location =
                "fallback/appliedMaterialPaths/" + std::to_string(occurrence) + "/" +
                std::to_string(index);
            EmitReference(ctx, AssetReferenceFieldKind::AppliedMaterialPath, location, value, false);
            ++index;
            cursor = q2 + 1;
        }
        ++occurrence;
        pos = end + 1;
    }
}

int RewriteFallbackQuotedPaths(std::string& text,
                               const std::string& oldPath,
                               const std::string& newPath) {
    // Replace exact quoted path occurrences (case-insensitive via KeysEqual check per match).
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find('"', pos)) != std::string::npos) {
        size_t end = text.find('"', pos + 1);
        if (end == std::string::npos) {
            break;
        }
        std::string value = text.substr(pos + 1, end - pos - 1);
        if (AssetPathPolicy::KeysEqual(value, oldPath)) {
            text.replace(pos + 1, end - pos - 1, newPath);
            end = pos + 1 + newPath.size();
            ++count;
        }
        pos = end + 1;
    }
    return count;
}

std::vector<AssetReference> CollectReferencesFromText(const std::filesystem::path& projectRoot,
                                                      const std::string& documentKey,
                                                      const std::filesystem::path& absolutePath,
                                                      const std::string& text,
                                                      std::vector<std::string>* diagnostics) {
    std::vector<AssetReference> refs;
    const std::string ext = ExtensionLower(absolutePath);

    CollectContext ctx;
    ctx.projectRoot = &projectRoot;
    ctx.documentKey = documentKey;
    ctx.extension = ext;
    ctx.allowAdvisoryNested = (ext == ".hpprefab");
    ctx.out = &refs;

    json root;
    try {
        root = json::parse(text);
        WalkCollect(ctx, root, "", "");
    } catch (const json::exception& ex) {
        if (diagnostics) {
            diagnostics->push_back("Malformed JSON in " + documentKey + ": " + ex.what());
        }
        if (ext == ".hpscene") {
            CollectFallbackSceneKeys(ctx, text);
            if (diagnostics) {
                diagnostics->push_back("Used string-search fallback for scene " + documentKey);
            }
        }
    }

    return refs;
}

} // namespace

const char* ToString(AssetReferenceFieldKind kind) {
    switch (kind) {
    case AssetReferenceFieldKind::MeshSource:
        return "MeshSource";
    case AssetReferenceFieldKind::AppliedMaterialPath:
        return "AppliedMaterialPath";
    case AssetReferenceFieldKind::TexturePath:
        return "TexturePath";
    case AssetReferenceFieldKind::ScriptPath:
        return "ScriptPath";
    case AssetReferenceFieldKind::ClipPath:
        return "ClipPath";
    case AssetReferenceFieldKind::HdriPath:
        return "HdriPath";
    case AssetReferenceFieldKind::PrefabPath:
        return "PrefabPath";
    case AssetReferenceFieldKind::ResourcePath:
        return "ResourcePath";
    case AssetReferenceFieldKind::ImageId:
        return "ImageId";
    case AssetReferenceFieldKind::BackgroundImageId:
        return "BackgroundImageId";
    case AssetReferenceFieldKind::Controller:
        return "Controller";
    case AssetReferenceFieldKind::DefaultScene:
        return "DefaultScene";
    case AssetReferenceFieldKind::LastScene:
        return "LastScene";
    case AssetReferenceFieldKind::DefaultInputProfile:
        return "DefaultInputProfile";
    case AssetReferenceFieldKind::NestedAssetPath:
        return "NestedAssetPath";
    case AssetReferenceFieldKind::Unknown:
    default:
        return "Unknown";
    }
}

void AssetReferenceIndex::clearUnlocked() {
    m_dependencies.clear();
    m_referrers.clear();
    m_diagnostics.clear();
}

void AssetReferenceIndex::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    clearUnlocked();
}

void AssetReferenceIndex::SetProjectRoot(const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectRoot = projectRoot.lexically_normal();
}

std::filesystem::path AssetReferenceIndex::GetProjectRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_projectRoot;
}

void AssetReferenceIndex::ClearDiagnostics() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_diagnostics.clear();
}

std::vector<std::string> AssetReferenceIndex::GetDiagnostics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_diagnostics;
}

void AssetReferenceIndex::addDiagnostic(std::string message) {
    m_diagnostics.push_back(std::move(message));
}

std::string AssetReferenceIndex::makeLookupKey(const std::string& path) const {
    const std::string portable = AssetPathPolicy::ToPortableKey(m_projectRoot, path);
    return ToLowerCopy(AssetPathPolicy::NormalizeSeparators(portable));
}

std::string AssetReferenceIndex::toDocumentKey(const std::filesystem::path& absolutePath) const {
    return AssetPathPolicy::ToPortableKey(m_projectRoot, absolutePath.generic_string());
}

std::filesystem::path AssetReferenceIndex::resolveDocumentPath(const std::filesystem::path& path) const {
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    if (m_projectRoot.empty()) {
        return path.lexically_normal();
    }
    return (m_projectRoot / path).lexically_normal();
}

void AssetReferenceIndex::removeDocumentUnlocked(const std::string& documentKey) {
    const std::string docLookup = makeLookupKey(documentKey);
    auto depIt = m_dependencies.find(docLookup);
    if (depIt == m_dependencies.end()) {
        return;
    }

    for (const AssetReference& ref : depIt->second) {
        const std::string refLookup = makeLookupKey(ref.referencedPath);
        auto refIt = m_referrers.find(refLookup);
        if (refIt == m_referrers.end()) {
            continue;
        }
        auto& vec = refIt->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [&](const AssetReference& existing) {
                                     return AssetPathPolicy::KeysEqual(existing.documentPath, documentKey) &&
                                            existing.structuredLocation == ref.structuredLocation &&
                                            AssetPathPolicy::KeysEqual(existing.referencedPath, ref.referencedPath);
                                 }),
                  vec.end());
        if (vec.empty()) {
            m_referrers.erase(refIt);
        }
    }

    m_dependencies.erase(depIt);
}

void AssetReferenceIndex::addReferenceUnlocked(AssetReference ref) {
    if (ref.documentPath.empty() || ref.referencedPath.empty()) {
        return;
    }
    const std::string docLookup = makeLookupKey(ref.documentPath);
    const std::string refLookup = makeLookupKey(ref.referencedPath);
    m_dependencies[docLookup].push_back(ref);
    m_referrers[refLookup].push_back(std::move(ref));
}

void AssetReferenceIndex::scanDocumentUnlocked(const std::filesystem::path& absolutePath) {
    const std::string ext = ExtensionLower(absolutePath);
    if (!IsIndexableExtension(ext)) {
        return;
    }

    // Restrict .json to known project settings paths.
    if (ext == ".json") {
        const std::string portable = toDocumentKey(absolutePath);
        const std::string lower = ToLowerCopy(portable);
        if (lower.find("project_settings.json") == std::string::npos &&
            lower.find(".heproject") == std::string::npos) {
            // Still allow if filename is project_settings.json anywhere under .hephaestus
            if (ToLowerCopy(absolutePath.filename().string()) != "project_settings.json") {
                return;
            }
        }
    }

    std::string text;
    std::string error;
    if (!ReadFileText(absolutePath, text, &error)) {
        addDiagnostic(error);
        return;
    }

    const std::string documentKey = toDocumentKey(absolutePath);
    removeDocumentUnlocked(documentKey);

    std::vector<std::string> localDiagnostics;
    auto refs = CollectReferencesFromText(m_projectRoot, documentKey, absolutePath, text, &localDiagnostics);
    for (auto& msg : localDiagnostics) {
        addDiagnostic(std::move(msg));
    }
    for (auto& ref : refs) {
        addReferenceUnlocked(std::move(ref));
    }
}

void AssetReferenceIndex::ScanDocument(const std::filesystem::path& absoluteOrRelativeDocumentPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::filesystem::path absolute = resolveDocumentPath(absoluteOrRelativeDocumentPath);
    if (!std::filesystem::exists(absolute)) {
        addDiagnostic("ScanDocument missing file: " + absolute.generic_string());
        return;
    }
    scanDocumentUnlocked(absolute);
}

void AssetReferenceIndex::ScanProject(const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    clearUnlocked();
    m_projectRoot = projectRoot.lexically_normal();

    const std::filesystem::path assetsDir = m_projectRoot / "assets";
    std::error_code ec;
    if (std::filesystem::exists(assetsDir, ec)) {
        for (std::filesystem::recursive_directory_iterator it(assetsDir, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                addDiagnostic("ScanProject walk error under assets/: " + ec.message());
                break;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const auto& path = it->path();
            const std::string ext = ExtensionLower(path);
            if (ext == ".hpscene" || ext == ".hpprefab" || ext == ".hematerial" || ext == ".hui") {
                scanDocumentUnlocked(path);
            }
        }
    }

    // Project file(s) at root.
    for (std::filesystem::directory_iterator it(m_projectRoot, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        if (ExtensionLower(it->path()) == ".heproject") {
            scanDocumentUnlocked(it->path());
        }
    }

    const std::filesystem::path settings = m_projectRoot / ".hephaestus" / "project_settings.json";
    if (std::filesystem::exists(settings, ec)) {
        scanDocumentUnlocked(settings);
    }
}

std::vector<AssetReference> AssetReferenceIndex::GetDependencies(const std::string& assetPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string key = makeLookupKey(assetPath);
    auto it = m_dependencies.find(key);
    if (it == m_dependencies.end()) {
        return {};
    }
    return it->second;
}

std::vector<AssetReference> AssetReferenceIndex::GetReferrers(const std::string& assetPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string key = makeLookupKey(assetPath);
    auto it = m_referrers.find(key);
    if (it == m_referrers.end()) {
        return {};
    }
    return it->second;
}

bool AssetReferenceIndex::RewritePathInDocument(const std::filesystem::path& documentPath,
                                                const std::filesystem::path& projectRoot,
                                                const std::string& oldPath,
                                                const std::string& newPath,
                                                int* rewriteCountOut,
                                                std::string* errorOut) {
    if (rewriteCountOut) {
        *rewriteCountOut = 0;
    }

    std::string text;
    if (!ReadFileText(documentPath, text, errorOut)) {
        return false;
    }

    const std::string ext = ExtensionLower(documentPath);
    const std::string portableNew = AssetPathPolicy::ToPortableKey(projectRoot, newPath);
    const std::string replacement = portableNew.empty() ? newPath : portableNew;
    const bool allowAdvisory = (ext == ".hpprefab");

    json root;
    try {
        root = json::parse(text);
        const int count = WalkRewrite(root, ext, allowAdvisory, "", oldPath, replacement);
        if (rewriteCountOut) {
            *rewriteCountOut = count;
        }
        if (count > 0) {
            const std::string written = root.dump(2);
            if (!WriteFileText(documentPath, written, errorOut)) {
                return false;
            }
        }
        return true;
    } catch (const json::exception& ex) {
        if (ext != ".hpscene") {
            if (errorOut) {
                *errorOut = std::string("Malformed JSON: ") + ex.what();
            }
            return false;
        }
        const int count = RewriteFallbackQuotedPaths(text, oldPath, replacement);
        if (rewriteCountOut) {
            *rewriteCountOut = count;
        }
        if (count > 0) {
            if (!WriteFileText(documentPath, text, errorOut)) {
                return false;
            }
        }
        return true;
    }
}

AssetReferenceRewriteResult AssetReferenceIndex::RewriteReferences(const std::string& oldPath,
                                                                   const std::string& newPath) {
    AssetReferenceRewriteResult result;

    std::vector<AssetReference> referrers;
    std::filesystem::path projectRoot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectRoot = m_projectRoot;
        const std::string key = makeLookupKey(oldPath);
        auto it = m_referrers.find(key);
        if (it != m_referrers.end()) {
            referrers = it->second;
        }
    }

    if (referrers.empty()) {
        return result;
    }

    std::vector<std::string> uniqueDocuments;
    uniqueDocuments.reserve(referrers.size());
    for (const AssetReference& ref : referrers) {
        if (std::none_of(uniqueDocuments.begin(), uniqueDocuments.end(), [&](const std::string& d) {
                return AssetPathPolicy::KeysEqual(d, ref.documentPath);
            })) {
            uniqueDocuments.push_back(ref.documentPath);
        }
    }

    for (const std::string& documentKey : uniqueDocuments) {
        const std::filesystem::path abs =
            AssetPathPolicy::ResolvePortableKey(projectRoot, documentKey);
        if (abs.empty() || !std::filesystem::exists(abs)) {
            result.success = false;
            result.errors.push_back("Missing referrer document: " + documentKey);
            continue;
        }

        int count = 0;
        std::string error;
        if (!RewritePathInDocument(abs, projectRoot, oldPath, newPath, &count, &error)) {
            result.success = false;
            result.errors.push_back(documentKey + ": " + (error.empty() ? "rewrite failed" : error));
            continue;
        }

        if (count > 0) {
            ++result.documentsTouched;
            result.referencesRewritten += count;
            ScanDocument(abs);
        }
    }

    return result;
}

} // namespace Hephaestus
