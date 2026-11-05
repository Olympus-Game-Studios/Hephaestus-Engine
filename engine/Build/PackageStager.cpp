#include "Build/PackageStager.h"
#include "../Plugins/PluginContent.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Hephaestus::Build {
namespace {

bool Cancelled(const BuildRequest& request) {
    return request.isCancelled && request.isCancelled();
}

void Problem(PackageStageResult& result, ProblemSeverity severity, std::string code,
             std::string message, std::string detail = {}) {
    result.problems.push_back(
        {severity, std::move(code), std::move(message), std::move(detail)});
}

bool SameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(right, ec) ||
        std::filesystem::file_size(left, ec) != std::filesystem::file_size(right, ec) || ec) {
        return false;
    }
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    constexpr std::size_t BUFFER_SIZE = 64 * 1024;
    char leftBuffer[BUFFER_SIZE];
    char rightBuffer[BUFFER_SIZE];
    while (a && b) {
        a.read(leftBuffer, sizeof(leftBuffer));
        b.read(rightBuffer, sizeof(rightBuffer));
        if (a.gcount() != b.gcount() ||
            !std::equal(leftBuffer, leftBuffer + a.gcount(), rightBuffer)) {
            return false;
        }
    }
    return true;
}

bool CopyOne(const std::filesystem::path& source,
             const std::filesystem::path& destination,
             const std::filesystem::path& previous,
             bool incremental,
             std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "Could not create " + destination.parent_path().generic_string() + ": " + ec.message();
        return false;
    }
    const auto copySource =
        incremental && !previous.empty() && SameFile(source, previous) ? previous : source;
    std::filesystem::copy_file(copySource, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Could not copy " + source.generic_string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool CopyTree(const std::filesystem::path& source,
              const std::filesystem::path& destination,
              const std::filesystem::path& previous,
              const std::string& stageName,
              const BuildRequest& request,
              PackageStageResult& result) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        return true;
    }
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(
             source, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && !it->is_symlink(ec)) {
            files.push_back(it->path());
        }
    }
    if (ec) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_TRAVERSAL_FAILED",
                "Could not traverse package source", source.generic_string() + ": " + ec.message());
        return false;
    }
    std::sort(files.begin(), files.end());
    if (request.onProgress) {
        request.onProgress(0.0F, stageName);
    }
    for (std::size_t index = 0; index < files.size(); ++index) {
        const auto& file = files[index];
        if (Cancelled(request)) {
            result.cancelled = true;
            return false;
        }
        if (request.onProgress) {
            request.onProgress(
                0.0F, stageName + " " + std::to_string(index + 1) + " / " +
                          std::to_string(files.size()));
        }
        const auto relative = file.lexically_relative(source);
        std::string error;
        if (!CopyOne(file, destination / relative, previous / relative,
                     request.incremental && !request.clean, error)) {
            Problem(result, ProblemSeverity::Error, "PACKAGE_COPY_FAILED",
                    "Could not copy package file", error);
            return false;
        }
    }
    return true;
}

bool WriteText(const std::filesystem::path& path, const std::string& text,
               PackageStageResult& result) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_WRITE_FAILED",
                "Could not write package metadata", path.generic_string());
        return false;
    }
    stream << text;
    return static_cast<bool>(stream);
}

std::vector<FileSizeEntry> Inventory(const std::filesystem::path& root) {
    std::vector<FileSizeEntry> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            files.push_back({it->path().lexically_relative(root).generic_string(),
                             it->file_size(ec)});
        }
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    return files;
}

std::string LicenseNotices(const std::filesystem::path& engineRoot) {
    std::ifstream stream(engineRoot / "THIRD_PARTY_NOTICES.md", std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream output;
    output << stream.rdbuf();
    if (!output || stream.bad()) {
        return {};
    }
    return output.str();
}

std::string PowerShellLiteral(std::string value) {
    std::size_t position = 0;
    while ((position = value.find('\'', position)) != std::string::npos) {
        value.insert(position, "'");
        position += 2;
    }
    return "'" + value + "'";
}

std::string UtcTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

bool IsLockedForReplacement(const std::filesystem::path& path) {
#ifdef _WIN32
    if (!std::filesystem::is_regular_file(path)) {
        return false;
    }
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return true;
    }
    CloseHandle(handle);
#else
    (void)path;
#endif
    return false;
}

#ifdef __APPLE__
std::string XmlEscape(const std::string& text) {
    std::string escaped;
    for (const char character : text) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            default: escaped.push_back(character);
        }
    }
    return escaped;
}

std::string BundleIdPart(const std::string& text) {
    std::string part;
    for (const unsigned char character : text) {
        if (std::isalnum(character)) {
            part.push_back(static_cast<char>(std::tolower(character)));
        } else if (!part.empty() && part.back() != '-') {
            part.push_back('-');
        }
    }
    while (!part.empty() && part.back() == '-') {
        part.pop_back();
    }
    return part.empty() ? "hephaestus" : part;
}

std::string MacInfoPlist(const BuildRequest& request, const std::string& product) {
    const std::string name = XmlEscape(request.productName.empty() ? product : request.productName);
    const std::string version = XmlEscape(request.version);
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n<dict>\n"
           "\t<key>CFBundleExecutable</key>\n\t<string>" + product + "</string>\n"
           "\t<key>CFBundleIdentifier</key>\n\t<string>com." + BundleIdPart(request.company) + "." +
               BundleIdPart(product) + "</string>\n"
           "\t<key>CFBundleName</key>\n\t<string>" + name + "</string>\n"
           "\t<key>CFBundleDisplayName</key>\n\t<string>" + name + "</string>\n"
           "\t<key>CFBundleShortVersionString</key>\n\t<string>" + version + "</string>\n"
           "\t<key>CFBundleVersion</key>\n\t<string>" + version + "</string>\n"
           "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n"
           "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n"
           "\t<key>CFBundleIconFile</key>\n\t<string>MacOS-Icon</string>\n"
           "\t<key>CFBundleIconName</key>\n\t<string>MacOS-Icon</string>\n"
           "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n"
           "</dict>\n</plist>\n";
}
#endif

} // namespace

std::filesystem::path PackageStager::contentDirectory(const std::filesystem::path& package,
                                                      const std::string& product) {
#ifdef __APPLE__
    return package / (product + ".app") / "Contents" / "Resources";
#else
    (void)product;
    return package;
#endif
}

std::filesystem::path PackageStager::launchPath(const std::filesystem::path& package,
                                                const std::string& product) {
#ifdef __APPLE__
    return package / (product + ".app");
#else
    return package / (product + kExecutableSuffix);
#endif
}

std::string PackageStager::sanitizeProductName(const std::string& productName) {
    std::string result;
    result.reserve(productName.size());
    for (unsigned char character : productName) {
        if (std::isalnum(character) || character == '-' || character == '_') {
            result.push_back(static_cast<char>(character));
        } else if (std::isspace(character) && !result.empty() && result.back() != '_') {
            result.push_back('_');
        }
    }
    while (!result.empty() && (result.back() == '_' || result.back() == '.')) {
        result.pop_back();
    }
    if (result.empty()) {
        return "HephaestusGame";
    }
    std::string upper = result;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    static const std::set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
        "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    if (reserved.contains(upper)) {
        result.insert(result.begin(), '_');
    }
    return result;
}

PackageStageResult PackageStager::clean(const BuildRequest& request) const {
    PackageStageResult result;
    const std::string product = sanitizeProductName(request.productName);
    const auto output = request.outputRoot / product;
    result.outputPath = output;

    if (IsLockedForReplacement(output / (product + kExecutableSuffix)) ||
        IsLockedForReplacement(output / "lua54.dll") ||
        IsLockedForReplacement(output / "lua.dll")) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_OUTPUT_LOCKED",
                "Cannot clean the current package because a runtime file is in use",
                "Stop the packaged game and try again.");
        return result;
    }

    std::error_code ec;
    for (const auto& path : {
             request.outputRoot / ("." + product + "_building"),
             request.outputRoot / ("." + product + "_previous"),
             output}) {
        std::filesystem::remove_all(path, ec);
        if (ec) {
            Problem(result, ProblemSeverity::Error, "PACKAGE_CLEAN_FAILED",
                    "Could not remove package output",
                    path.generic_string() + ": " + ec.message());
            return result;
        }
    }
    result.success = true;
    return result;
}

PackageStageResult PackageStager::stage(const BuildRequest& request,
                                        const RuntimeArtifacts& runtime) const {
    PackageStageResult result;
    const std::string product = sanitizeProductName(request.productName);
    const auto output = request.outputRoot / product;
    const auto staging = request.outputRoot / ("." + product + "_building");
    const auto backup = request.outputRoot / ("." + product + "_previous");
    const auto content = contentDirectory(staging, product);
    const auto previousContent = contentDirectory(output, product);
    result.outputPath = output;

    if (IsLockedForReplacement(output / (product + kExecutableSuffix)) ||
        IsLockedForReplacement(output / "lua54.dll") ||
        IsLockedForReplacement(output / "lua.dll")) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_OUTPUT_LOCKED",
                "Cannot replace the current package because a runtime file is in use",
                "Stop the packaged game or choose a different output directory.");
        return result;
    }

    if (request.onProgress) {
        request.onProgress(0.0F, "Creating staging directory");
    }
    std::error_code ec;
    std::filesystem::create_directories(request.outputRoot, ec);
    std::filesystem::remove_all(staging, ec);
    ec.clear();
    std::filesystem::create_directories(staging, ec);
    if (ec) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_STAGE_CREATE_FAILED",
                "Could not create transactional staging directory", ec.message());
        return result;
    }

    const auto fail = [&]() {
        std::error_code cleanupError;
        std::filesystem::remove_all(staging, cleanupError);
        return result;
    };

    if (Cancelled(request)) {
        result.cancelled = true;
        return fail();
    }
    if (!CopyTree(request.projectRoot / "assets", content / "assets", previousContent / "assets",
                  "Copying assets", request, result) ||
        !CopyTree(request.projectRoot / "scripts", content / "scripts", previousContent / "scripts",
                  "Copying scripts", request, result) ||
        !CopyTree(request.engineRoot / ".engine" / "meshes", content / ".engine" / "meshes",
                  previousContent / ".engine" / "meshes", "Copying builtin meshes", request, result) ||
        !CopyTree(request.engineRoot / ".engine" / "hdris", content / ".engine" / "hdris",
                  previousContent / ".engine" / "hdris", "Copying builtin HDRIs", request, result) ||
        !CopyTree(request.projectRoot / ".engine", content / ".engine",
                  previousContent / ".engine", "Copying project runtime content", request, result) ||
        !CopyTree(runtime.shadersDirectory, content / "shaders", previousContent / "shaders",
                  "Copying shaders", request, result)) {
        return fail();
    }

    std::string copyError;
#ifdef __APPLE__
    // The player bundle, with its Frameworks and icon, becomes <product>.app.
    const auto app = launchPath(staging, product);
    const auto playerBundle = runtime.executable.parent_path().parent_path().parent_path();
    if (playerBundle.extension() != ".app" || !std::filesystem::is_regular_file(runtime.executable, ec)) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_RUNTIME_COPY_FAILED",
                "Could not copy runtime prerequisite",
                runtime.executable.generic_string() + " is not inside a player .app bundle");
        return fail();
    }
    if (!CopyTree(playerBundle, app, launchPath(output, product), "Copying player", request,
                  result)) {
        return fail();
    }
    std::filesystem::rename(app / "Contents" / "MacOS" / runtime.executable.filename(),
                            app / "Contents" / "MacOS" / product, ec);
    if (ec) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_RUNTIME_COPY_FAILED",
                "Could not name the game executable", ec.message());
        return fail();
    }
    if (!WriteText(app / "Contents" / "Info.plist", MacInfoPlist(request, product), result)) {
        return fail();
    }
#else
    const auto executable = staging / (product + kExecutableSuffix);
    if (!CopyOne(runtime.executable, executable, output / (product + kExecutableSuffix),
                 request.incremental && !request.clean, copyError) ||
        (kRuntimeShipsLua &&
         !CopyOne(runtime.luaLibrary, staging / "lua54.dll", output / "lua54.dll",
                  request.incremental && !request.clean, copyError))) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_RUNTIME_COPY_FAILED",
                "Could not copy runtime prerequisite", copyError);
        return fail();
    }
#endif
    const auto layerSource = request.projectRoot / ".hephaestus" / "physics_layers.json";
    if (std::filesystem::exists(layerSource)) {
        std::filesystem::create_directories(content / ".hephaestus", ec);
        if (!CopyOne(layerSource, content / ".hephaestus" / "physics_layers.json",
                     previousContent / ".hephaestus" / "physics_layers.json",
                     request.incremental && !request.clean, copyError)) {
            Problem(result, ProblemSeverity::Error, "PACKAGE_LAYER_COPY_FAILED",
                    "Could not copy physics_layers.json", copyError);
            return fail();
        }
    }

    const auto missingPlugins = Hephaestus::Plugins::FindMissingEnabledPlugins(
        request.engineRoot, request.projectRoot);
    for (const auto& id : missingPlugins) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_PLUGIN_NOT_FOUND",
                "Enabled plugin " + id + " was not found, so its content cannot be packaged",
                "Searched " + (request.engineRoot / "Plugins").generic_string() + " and " +
                    (request.projectRoot / "Plugins").generic_string());
    }
    if (!missingPlugins.empty()) {
        return fail();
    }

    std::vector<Hephaestus::Plugins::StagedPluginMount> stagedMounts;
    const auto pluginContent = Hephaestus::Plugins::CollectEnabledPluginContent(
        request.engineRoot, request.projectRoot);
    for (const auto& plugin : pluginContent) {
        const auto pluginRelative = std::filesystem::path("Plugins") / plugin.id;
        Hephaestus::Plugins::StagedPluginMount mount{plugin.id, {}, {}};
        if (!plugin.sourceRoot.empty()) {
            const auto relative = pluginRelative / "Content";
            if (!CopyTree(plugin.sourceRoot, content / relative, previousContent / relative,
                          "Copying plugin content " + plugin.id, request, result)) {
                return fail();
            }
            mount.relativeRoot = relative;
        }
        if (!plugin.runtimeEntry.empty()) {
            const auto module = plugin.pluginRoot / plugin.runtimeEntry;
            if (!std::filesystem::is_regular_file(module, ec)) {
                Problem(result, ProblemSeverity::Error, "PACKAGE_PLUGIN_RUNTIME_MISSING",
                        "Plugin " + plugin.id + " runtime module was not found",
                        module.generic_string() + " (build the plugin before packaging)");
                return fail();
            }
            // The module's whole folder ships so a language runtime can bring its own libraries.
            const auto folder = plugin.runtimeEntry.parent_path();
            const auto staged = pluginRelative / plugin.runtimeEntry;
            if (folder.empty()) {
                if (!CopyOne(module, content / staged, previousContent / staged,
                             request.incremental && !request.clean, copyError)) {
                    Problem(result, ProblemSeverity::Error, "PACKAGE_PLUGIN_RUNTIME_COPY_FAILED",
                            "Could not copy plugin runtime module", copyError);
                    return fail();
                }
            } else if (!CopyTree(plugin.pluginRoot / folder, content / pluginRelative / folder,
                                 previousContent / pluginRelative / folder,
                                 "Copying plugin runtime " + plugin.id, request, result)) {
                return fail();
            }
            mount.runtimeEntry = staged;
        }
        stagedMounts.push_back(std::move(mount));
    }
    {
        std::string mountsError;
        if (!Hephaestus::Plugins::WriteStagedPluginMounts(content, stagedMounts, mountsError)) {
            Problem(result, ProblemSeverity::Error, "PACKAGE_PLUGIN_MOUNTS_FAILED",
                    "Could not write staged plugin content mounts", mountsError);
            return fail();
        }
    }

    const std::string startup = request.startupScene;
    if (request.onProgress) {
        request.onProgress(0.0F, "Writing configuration");
    }
    std::ostringstream ini;
    ini << "[Game]\nproject_name = " << request.productName
        << "\nwindow_title = "
        << (request.window.title.empty() ? request.productName : request.window.title)
        << "\nscene = " << startup
        << "\ninput_profile = " << request.inputProfile
        << "\n\n[Display]\nwindow_width = " << request.window.width
        << "\nwindow_height = " << request.window.height << "\nfullscreen = "
        << (request.window.fullscreen ? "true" : "false") << "\nvsync = "
        << (request.window.vsync ? "true" : "false") << "\n";
    if (request.enableRuntimeAutomation &&
        request.configuration == BuildConfiguration::Development) {
        ini << "\n[Automation]\nenabled = true\nport = 0\n";
    }
#ifdef __APPLE__
    const std::string requirements =
        "Open " + launchPath({}, product).string() + " to start the game.\n"
        "Requires macOS on Apple silicon or Intel with Metal support.\n";
#elif defined(__linux__)
    const std::string requirements =
        "Run ./" + product + " to start the game.\n"
        "Requires Linux, a Vulkan-capable GPU with current drivers, and the system Lua "
        "shared library.\n";
#else
    const std::string requirements =
        "Run " + product + kExecutableSuffix + " to start the game.\n"
        "Requires Windows x64, a Vulkan-capable GPU with current drivers, "
        "and the Microsoft Visual C++ 2015-2022 x64 Redistributable.\n";
#endif
    if (!WriteText(content / "game.ini", ini.str(), result) ||
        !WriteText(staging / "README.txt",
                   request.productName + " " + request.version + "\n\n" + requirements +
                   "The Hephaestus Editor, engine source, CMake, and Vulkan SDK are "
                   "not required to run this package.\n", result)) {
        return fail();
    }
    const std::string notices = LicenseNotices(request.engineRoot);
    if (notices.empty()) {
        Problem(result, ProblemSeverity::Warning, "THIRD_PARTY_NOTICES_MISSING",
                "Third-party notices were not found",
                "Expected " + (request.engineRoot / "THIRD_PARTY_NOTICES.md").generic_string() +
                "; the package has no ThirdPartyNotices.txt.");
    } else if (!WriteText(content / "ThirdPartyNotices.txt", notices, result)) {
        return fail();
    }
    if (product != request.productName) {
        Problem(result, ProblemSeverity::Warning, "PACKAGE_PRODUCT_NAME_SANITIZED",
                "The product name was adjusted for a Windows filename",
                request.productName + " -> " + product);
    }

#ifdef __APPLE__
    // Renaming the executable and rewriting Info.plist invalidate the player's signature.
    if (RunQuietly({"codesign", "--force", "--sign", "-", launchPath(staging, product).string()}) != 0) {
        Problem(result, ProblemSeverity::Warning, "PACKAGE_SIGN_FAILED",
                "Could not ad-hoc sign the game's app bundle",
                "Run codesign --force --sign - on " + launchPath(output, product).string());
    }
#endif

    BuildManifestData manifest;
    manifest.productName = request.productName;
    manifest.version = request.version;
    manifest.company = request.company;
    manifest.engineVersion = request.engineVersion;
    manifest.configuration = ToString(request.configuration);
    manifest.timestampUtc = UtcTimestamp();
    manifest.startupScene = startup;
    manifest.executable = launchPath({}, product).generic_string();
    manifest.files = Inventory(staging);
    std::string manifestError;
    if (!BuildManifest::writeJson(staging / "build_manifest.json", manifest, &manifestError) ||
        !BuildManifest::writeExportList(staging / "export_manifest.txt", manifest, &manifestError)) {
        Problem(result, ProblemSeverity::Error, "PACKAGE_MANIFEST_FAILED",
                "Could not write package manifests", manifestError);
        return fail();
    }

    if (Cancelled(request)) {
        result.cancelled = true;
        return fail();
    }

    if (request.onProgress) {
        request.onProgress(0.0F, "Finalising package");
    }
    std::filesystem::remove_all(backup, ec);
    ec.clear();
    if (std::filesystem::exists(output, ec)) {
        std::filesystem::rename(output, backup, ec);
        if (ec) {
            Problem(result, ProblemSeverity::Error, "PACKAGE_OUTPUT_LOCKED",
                    "Could not replace the previous package",
                    "The packaged game may still be running, or a file is locked: " +
                        ec.message());
            return fail();
        }
    }
    std::filesystem::rename(staging, output, ec);
    if (ec) {
        std::error_code restoreError;
        if (std::filesystem::exists(backup, restoreError)) {
            std::filesystem::rename(backup, output, restoreError);
        }
        Problem(result, ProblemSeverity::Error, "PACKAGE_COMMIT_FAILED",
                "Could not commit the staged package", ec.message());
        return fail();
    }
    std::filesystem::remove_all(backup, ec);

    const auto inventory = Inventory(output);
    result.packageBytes = 0;
    for (const auto& file : inventory) {
        result.packageBytes += file.bytes;
    }
    result.largestFiles = inventory;
    std::sort(result.largestFiles.begin(), result.largestFiles.end(),
              [](const auto& a, const auto& b) {
                  return a.bytes != b.bytes ? a.bytes > b.bytes : a.path < b.path;
              });
    if (result.largestFiles.size() > 10) {
        result.largestFiles.resize(10);
    }
    if (request.createZip) {
#ifdef _WIN32
        const auto zipPath = request.outputRoot / (product + ".zip");
        const std::string command =
            "powershell -NoProfile -NonInteractive -Command \"Compress-Archive -Path " +
            PowerShellLiteral((output / "*").string()) + " -DestinationPath " +
            PowerShellLiteral(zipPath.string()) + " -Force\"";
        if (std::system(command.c_str()) != 0) {
            Problem(result, ProblemSeverity::Warning, "PACKAGE_ZIP_FAILED",
                    "The package succeeded but its ZIP archive could not be created",
                    zipPath.generic_string());
        }
#else
        Problem(result, ProblemSeverity::Warning, "PACKAGE_ZIP_UNSUPPORTED",
                "ZIP creation is currently supported only on Windows");
#endif
    }
    result.executablePath = launchPath(output, product);
    result.success = true;
    return result;
}

} // namespace Hephaestus::Build
