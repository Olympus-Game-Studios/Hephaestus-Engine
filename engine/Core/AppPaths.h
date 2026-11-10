#pragma once

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace Hephaestus {

// The executable's folder, or <X>.app/Contents when it runs inside a macOS bundle.
inline std::filesystem::path MacBundleContents(const std::filesystem::path& executableDirectory)
{
#ifdef __APPLE__
    if (executableDirectory.filename() == "MacOS" &&
        executableDirectory.parent_path().filename() == "Contents") {
        return executableDirectory.parent_path();
    }
#endif
    (void)executableDirectory;
    return {};
}

// True for a shipped .app that carries its own assets in Contents/Resources.
inline bool IsSelfContainedMacBundle(const std::filesystem::path& executableDirectory)
{
    const auto contents = MacBundleContents(executableDirectory);
    std::error_code ec;
    return !contents.empty() && std::filesystem::is_directory(contents / "Resources" / "assets", ec);
}

// Folder holding assets, shaders, Plugins and sibling tools: the executable's folder on
// Windows and Linux. A macOS .app uses Contents/Resources when it ships its assets, and
// otherwise the folder that contains the .app (the build tree, which matches Windows).
inline std::filesystem::path ResourceRootFor(const std::filesystem::path& executableDirectory)
{
    const auto contents = MacBundleContents(executableDirectory);
    if (contents.empty()) {
        return executableDirectory;
    }
    if (IsSelfContainedMacBundle(executableDirectory)) {
        return contents / "Resources";
    }
    return contents.parent_path().parent_path();
}

// The folder holding the running executable; empty if the platform can't say.
std::filesystem::path ExecutableDirectory();

// %LOCALAPPDATA%\<appName> on Windows, where logs and crash dumps go so the install folder
// stays read-only; empty on other platforms or when Windows can't provide the folder.
std::filesystem::path LocalAppDataFor(const std::filesystem::path& appName);

// Where a game keeps per-user data such as settings and saves: LocalAppDataFor on Windows,
// ~/Library/Application Support/<appName> on macOS, $XDG_DATA_HOME (or ~/.local/share)/<appName> elsewhere.
// Empty when none can be found.
std::filesystem::path UserDataFor(const std::filesystem::path& appName);

// Where logs go when LocalAppDataFor is empty: beside the resources, except a shipped .app
// (which must not modify itself) logs to ~/Library/Logs/<app name>.
inline std::filesystem::path LogRootFor(const std::filesystem::path& executableDirectory)
{
    if (IsSelfContainedMacBundle(executableDirectory)) {
        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / "Library" / "Logs" /
                   MacBundleContents(executableDirectory).parent_path().stem();
        }
    }
    return ResourceRootFor(executableDirectory);
}

} // namespace Hephaestus
