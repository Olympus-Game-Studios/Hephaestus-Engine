#include "Build/ProjectValidator.h"

#include "Assets/AssetPathPolicy.h"
#include "Assets/AssetReferenceIndex.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Hephaestus::Build {
namespace {

using json = nlohmann::json;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsDocument(const std::filesystem::path& path) {
    const std::string ext = Lower(path.extension().string());
    return ext == ".hpscene" || ext == ".hpprefab" || ext == ".hematerial" || ext == ".hui";
}

std::vector<std::filesystem::path> Documents(const std::filesystem::path& projectRoot) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    const auto assets = projectRoot / "assets";
    if (std::filesystem::is_directory(assets, ec)) {
        for (std::filesystem::recursive_directory_iterator it(assets, ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            if (it->is_regular_file(ec) && IsDocument(it->path())) {
                result.push_back(it->path());
            }
        }
    }
    for (std::filesystem::directory_iterator it(projectRoot, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".heproject") {
            result.push_back(it->path());
        }
    }
    const auto settings = projectRoot / ".hephaestus" / "project_settings.json";
    if (std::filesystem::is_regular_file(settings, ec)) {
        result.push_back(settings);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string KeyFor(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    return Hephaestus::AssetPathPolicy::ToPortableKey(projectRoot, path.generic_string());
}

void Add(std::vector<EditorProblem>& out, ProblemSeverity severity, std::string code,
         std::string message, std::string detail = {}, std::string assetPath = {}) {
    out.push_back({severity, std::move(code), std::move(message), std::move(detail),
                   std::move(assetPath)});
}

bool ReadJson(const std::filesystem::path& path, json& out, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open " + path.generic_string();
        return false;
    }
    try {
        stream >> out;
        return true;
    } catch (const json::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool IsKnownReferenceKey(const std::string& key) {
    static const std::set<std::string> keys = {
        "meshSource", "appliedMaterialPaths", "albedoTexturePath", "normalTexturePath",
        "roughnessTexturePath", "metallicTexturePath", "aoTexturePath", "scriptPath",
        "clipPath", "hdriPath", "hdrPath", "environmentPath", "skyboxPath", "skybox",
        "prefabPath", "blueprintPath", "resourcePath", "imageId", "backgroundImageId",
        "controller", "defaultScene", "lastScene"
    };
    return keys.contains(key);
}

void CollectRawPaths(const json& node, const std::string& parentKey,
                     std::vector<std::string>& paths) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.value().is_string() && IsKnownReferenceKey(it.key())) {
                paths.push_back(it.value().get<std::string>());
            } else {
                CollectRawPaths(it.value(), it.key(), paths);
            }
        }
    } else if (node.is_array()) {
        for (const auto& value : node) {
            if (value.is_string() && parentKey == "appliedMaterialPaths") {
                paths.push_back(value.get<std::string>());
            } else {
                CollectRawPaths(value, parentKey, paths);
            }
        }
    }
}

std::string JsonStartup(const json& root) {
    if (root.contains("defaultScene") && root["defaultScene"].is_string()) {
        return root["defaultScene"].get<std::string>();
    }
    if (root.contains("startup") && root["startup"].is_object() &&
        root["startup"].contains("defaultScene") && root["startup"]["defaultScene"].is_string()) {
        return root["startup"]["defaultScene"].get<std::string>();
    }
    if (root.contains("project") && root["project"].is_object() &&
        root["project"].contains("defaultScene") && root["project"]["defaultScene"].is_string()) {
        return root["project"]["defaultScene"].get<std::string>();
    }
    if (root.contains("projectInfo") && root["projectInfo"].is_object() &&
        root["projectInfo"].contains("defaultScene") &&
        root["projectInfo"]["defaultScene"].is_string()) {
        return root["projectInfo"]["defaultScene"].get<std::string>();
    }
    return {};
}

} // namespace

std::string ProjectValidator::determineStartupScene(const std::filesystem::path& projectRoot,
                                                    std::vector<EditorProblem>* problems) {
    std::string startup;
    std::vector<std::filesystem::path> projectFiles;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(projectRoot, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".heproject") {
            projectFiles.push_back(it->path());
        }
    }
    std::sort(projectFiles.begin(), projectFiles.end());
    projectFiles.push_back(projectRoot / ".hephaestus" / "project_settings.json");

    for (const auto& path : projectFiles) {
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        json root;
        std::string error;
        if (!ReadJson(path, root, error)) {
            if (problems) {
                Add(*problems, ProblemSeverity::Error, "PROJECT_JSON_MALFORMED",
                    "Project configuration contains malformed JSON", error,
                    KeyFor(projectRoot, path));
            }
            continue;
        }
        const std::string candidate = JsonStartup(root);
        if (!candidate.empty()) {
            startup = candidate;
        }
    }
    return startup;
}

std::vector<EditorProblem> ProjectValidator::validate(
    const std::filesystem::path& projectRoot,
    const ProjectValidationOptions& options) const {
    std::vector<EditorProblem> problems;
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec)) {
        Add(problems, ProblemSeverity::Error, "PROJECT_ROOT_MISSING",
            "Project root does not exist", projectRoot.generic_string());
        return problems;
    }

    Hephaestus::AssetReferenceIndex index;
    index.ScanProject(projectRoot);
    const auto documents = Documents(projectRoot);

    for (const auto& diagnostic : index.GetDiagnostics()) {
        const bool malformed = diagnostic.find("Malformed JSON") != std::string::npos;
        Add(problems, malformed ? ProblemSeverity::Error : ProblemSeverity::Warning,
            malformed ? "ASSET_JSON_MALFORMED" : "ASSET_INDEX_DIAGNOSTIC",
            malformed ? "Asset document contains malformed JSON" : "Asset indexing diagnostic",
            diagnostic);
    }

    std::set<std::string> emitted;
    for (const auto& document : documents) {
        const std::string documentKey = KeyFor(projectRoot, document);
        for (const auto& reference : index.GetDependencies(documentKey)) {
            if (reference.isAdvisory || reference.referencedPath.rfind("builtin://", 0) == 0) {
                continue;
            }
            const auto classification =
                Hephaestus::AssetPathPolicy::Classify(projectRoot, reference.referencedPath);
            const std::string pathIssueKey =
                Lower(documentKey + "|path|" + reference.referencedPath);
            if (classification.kind == Hephaestus::AssetPathKind::AbsoluteExternal &&
                emitted.insert(pathIssueKey).second) {
                Add(problems, ProblemSeverity::Error, "ASSET_PATH_EXTERNAL",
                    "External absolute asset path is not packageable",
                    reference.referencedPath, documentKey);
            } else if (classification.kind ==
                           Hephaestus::AssetPathKind::AbsoluteInProject &&
                       emitted.insert(pathIssueKey).second) {
                Add(problems, ProblemSeverity::Warning,
                    "ASSET_PATH_ABSOLUTE_IN_PROJECT",
                    "In-project reference should use a portable relative path",
                    "Use " + classification.portableKey, documentKey);
            }
            const auto resolved =
                Hephaestus::AssetPathPolicy::ResolvePortableKey(projectRoot, reference.referencedPath);
            const std::string unique = Lower(documentKey + "|" + reference.referencedPath);
            if (!resolved.empty() && !std::filesystem::exists(resolved, ec) && emitted.insert(unique).second) {
                EditorProblem problem{ProblemSeverity::Error, "ASSET_REFERENCE_MISSING",
                                      "Referenced asset does not exist", reference.structuredLocation,
                                      reference.referencedPath};
                problem.referrers.push_back(documentKey);
                problem.actions.push_back("Choose an existing project asset");
                problems.push_back(std::move(problem));
            }
            if (Lower(std::filesystem::path(reference.referencedPath).extension().string()) == ".ogg") {
                Add(problems, ProblemSeverity::Error, "AUDIO_OGG_UNSUPPORTED",
                    "Referenced .ogg audio is not supported by the packaged runtime",
                    "Convert the audio clip to a supported format.", reference.referencedPath);
            }
        }

        json root;
        std::string parseError;
        if (!ReadJson(document, root, parseError)) {
            continue;
        }
        std::vector<std::string> rawPaths;
        CollectRawPaths(root, {}, rawPaths);
        for (const auto& rawPath : rawPaths) {
            const auto classification = Hephaestus::AssetPathPolicy::Classify(projectRoot, rawPath);
            if (classification.kind == Hephaestus::AssetPathKind::AbsoluteExternal) {
                Add(problems, ProblemSeverity::Error, "ASSET_PATH_EXTERNAL",
                    "External absolute asset path is not packageable", rawPath, documentKey);
            } else if (classification.kind == Hephaestus::AssetPathKind::AbsoluteInProject) {
                Add(problems, ProblemSeverity::Warning, "ASSET_PATH_ABSOLUTE_IN_PROJECT",
                    "In-project reference should use a portable relative path",
                    "Use " + classification.portableKey, documentKey);
            }
        }
    }

    const std::string startup = options.startupScene.empty()
        ? determineStartupScene(projectRoot, &problems) : options.startupScene;
    if (startup.empty()) {
        Add(problems, ProblemSeverity::Error, "STARTUP_SCENE_UNSET",
            "No startup defaultScene is configured");
    } else {
        const auto startupPath = Hephaestus::AssetPathPolicy::ResolvePortableKey(projectRoot, startup);
        if (startupPath.empty() || !std::filesystem::is_regular_file(startupPath, ec)) {
            Add(problems, ProblemSeverity::Error, "STARTUP_SCENE_MISSING",
                "Startup scene does not exist", {}, startup);
        } else {
            json scene;
            std::string error;
            if (!ReadJson(startupPath, scene, error)) {
                Add(problems, ProblemSeverity::Error, "STARTUP_SCENE_MALFORMED",
                    "Startup scene contains malformed JSON", error, startup);
            }
        }
    }

    if (options.runtimeArtifacts) {
        const auto& runtime = *options.runtimeArtifacts;
        if (!std::filesystem::is_regular_file(runtime.executable, ec)) {
            Add(problems, ProblemSeverity::Error, "BUILD_RUNTIME_EXECUTABLE_MISSING",
                "Runtime executable is missing");
        }
        if (!std::filesystem::is_directory(runtime.shadersDirectory, ec)) {
            Add(problems, ProblemSeverity::Error, "BUILD_RUNTIME_SHADERS_MISSING",
                "Runtime shaders directory is missing");
        }
        if (!std::filesystem::is_regular_file(runtime.luaLibrary, ec)) {
            Add(problems, ProblemSeverity::Error, "BUILD_RUNTIME_LUA_MISSING",
                "Lua runtime library is missing");
        }
    }

    const auto assetsRoot = projectRoot / "assets";
    if (std::filesystem::is_directory(assetsRoot, ec)) {
        for (std::filesystem::recursive_directory_iterator it(
                 assetsRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string extension = Lower(it->path().extension().string());
            if (extension == ".blend" || extension == ".psd" ||
                extension == ".ma" || extension == ".mb") {
                Add(problems, ProblemSeverity::Info, "SOURCE_ASSET_PACKAGED",
                    "Source art under assets/ will be included in the package",
                    "Move authoring-only files outside assets/ to reduce package size.",
                    KeyFor(projectRoot, it->path()));
            }
        }
    }

    if (options.includeUnusedAssetAdvisories) {
        auto unused = findUnusedAssets(projectRoot, startup);
        problems.insert(problems.end(), unused.begin(), unused.end());
    }
    return problems;
}

std::vector<EditorProblem> ProjectValidator::findUnusedAssets(
    const std::filesystem::path& projectRoot, const std::string& startupScene) const {
    std::vector<EditorProblem> problems;
    Hephaestus::AssetReferenceIndex index;
    index.ScanProject(projectRoot);
    const std::string startup = startupScene.empty()
        ? determineStartupScene(projectRoot) : startupScene;
    std::error_code ec;
    const auto assets = projectRoot / "assets";
    if (!std::filesystem::is_directory(assets, ec)) {
        return problems;
    }
    for (std::filesystem::recursive_directory_iterator it(assets, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string key = KeyFor(projectRoot, it->path());
        if (!Hephaestus::AssetPathPolicy::KeysEqual(key, startup) &&
            index.GetReferrers(key).empty()) {
            Add(problems, ProblemSeverity::Info, "ASSET_UNUSED_ADVISORY",
                "Asset has no indexed referrers", {}, key);
        }
    }
    return problems;
}

} // namespace Hephaestus::Build
